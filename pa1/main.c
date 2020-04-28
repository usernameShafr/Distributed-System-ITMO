#include "main.h"

FILE *fd_log_file_events, *fd_log_file_pipe_descrs;
char buffer_temp[MAX_MESSAGE_LEN];
int process_self_id;
short process_amount, messages_started_received = 0, messages_done_received = 0;

void print_to_file_and_terminal(char* string_to_print) {
	fprintf(fd_log_file_events, "%s", string_to_print);
	fflush(fd_log_file_events);
	printf("%s", string_to_print);
}

void new_message_receive(int **storage_fds[2], Message *message)
{
	receive_any(storage_fds, message);
	if(message->s_header.s_type == STARTED) messages_started_received++;
	if(message->s_header.s_type == DONE) messages_done_received++;
}

int matrix_flat_get(int i, int j, int k) {return k + 2*j + i*2*(process_amount+1);}

int main(int argc, char** argv)
{
	process_amount = atoi(argv[2]);
	pid_t process_pid_main, process_pid_spawned;
	int storage_fds[process_amount + 1][process_amount+1][2];
	fd_log_file_events = fopen(events_log, "a");
	fd_log_file_pipe_descrs = fopen(pipes_log, "w");
	process_self_id = 0;

	for(int i=0;i<=process_amount;i++)
	{
		for(int j=0;j<=process_amount;j++)
		{
			if(i != j)
			{
				pipe(storage_fds[i][j]);
				fcntl(storage_fds[i][j][0], F_SETFL, O_NONBLOCK );
				fcntl(storage_fds[i][j][1], F_SETFL, O_NONBLOCK );
				fprintf(fd_log_file_pipe_descrs, "Opened pipe from process %d to process %d\n", i, j);
			}
			else
			{
				storage_fds[i][j][0] = storage_fds[i][j][1] = -1;
			}
		}
	}

	fflush(fd_log_file_pipe_descrs);
	process_pid_main = getpid();

	for(int i=1;i<=process_amount;i++)
	{
		process_pid_spawned = fork();
		if (process_pid_spawned == 0)
		{
			process_self_id = i;
			for (int j=0;j<=process_amount;j++)
			{
				for(int c=0;c<=process_amount;c++)
				{
					if(j!=c)
					{
						if(process_self_id!=j)
						{
							close(storage_fds[j][c][1]);
							storage_fds[j][c][1] = -1;
							fprintf(fd_log_file_pipe_descrs, "Closed WRITE pipe from process %d to process %d by worker %d\n", j, c, process_self_id);
						}
						if(process_self_id!=c)
						{
							close(storage_fds[j][c][0]);
							storage_fds[j][c][0] = -1;
							fprintf(fd_log_file_pipe_descrs, "Closed READ  pipe from process %d to process %d by worker %d\n", j, c, process_self_id);
						}
					}
				}
			}
			fflush(fd_log_file_pipe_descrs);
			Message message;
			message.s_header.s_magic = MESSAGE_MAGIC;
			message.s_header.s_type = STARTED;
			sprintf(message.s_payload, log_started_fmt, process_self_id, getpid(), process_pid_main);
			message.s_header.s_payload_len = strlen(message.s_payload);

			send_multicast(storage_fds, &message);

			print_to_file_and_terminal(message.s_payload);

			while(messages_started_received < process_amount -1) new_message_receive((int***)storage_fds, &message);

			sprintf(buffer_temp, log_received_all_started_fmt, process_self_id);
			print_to_file_and_terminal(buffer_temp);

			message.s_header.s_type = DONE;
			sprintf (message.s_payload, log_done_fmt, process_self_id);
			message.s_header.s_payload_len = strlen(message.s_payload);
			send_multicast(storage_fds, &message);
			print_to_file_and_terminal(message.s_payload);
			while(messages_done_received < process_amount -1) new_message_receive((int***)storage_fds, &message);

			sprintf(buffer_temp, log_received_all_done_fmt, process_self_id);
			print_to_file_and_terminal(buffer_temp);
			exit(0);
		}
	}
//только для родительского

	for (int j=0;j<=process_amount;j++)
	{
		for(int c=0;c<=process_amount;c++)
		{
			if(j!=c)
			{
				if(0!=j)
				{

					close(storage_fds[j][c][1]);
					storage_fds[j][c][1] = -1;
					fprintf(fd_log_file_pipe_descrs, "Closed WRITE pipe from process %d to process %d by main\n", j, c);
				}
				if(0!=c)
				{
					close(storage_fds[j][c][0]);
					storage_fds[j][c][0] = -1;
					fprintf(fd_log_file_pipe_descrs, "Closed READ  pipe from process %d to process %d by main\n", j, c);
				}
			}
		}
	}
	fflush(fd_log_file_pipe_descrs);

	Message message;
	int process_child_stopped_count = 0;
	while(messages_started_received < process_amount) new_message_receive((int***)storage_fds, &message);

    sprintf(buffer_temp, log_received_all_started_fmt, 0);
	print_to_file_and_terminal(buffer_temp);

	while(messages_done_received < process_amount)
	{
		new_message_receive((int***)storage_fds, &message);
	}

	sprintf(buffer_temp, log_received_all_done_fmt, 0);
	print_to_file_and_terminal(buffer_temp);

	while(process_child_stopped_count < process_amount)
	{
		wait(NULL);
		process_child_stopped_count++;
	}
	return 0;
}

