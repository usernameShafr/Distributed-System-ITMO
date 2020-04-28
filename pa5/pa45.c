#include "pa45.h"
#include "string.h"

FILE *fd_log_file_events, *fd_log_file_pipe_descrs;
char buffer_temp[MAX_MESSAGE_LEN];
int process_self_id;
short process_amount, messages_started_received = 0, messages_done_received = 0;
short *process_list_awaiting_reply, process_mutex_activated = 0, process_flag_done = 0;
timestamp_t process_own_time = 0, process_self_queue_time;
Message message;

timestamp_t get_lamport_time(){
    return process_own_time;
}

void print_to_file_and_terminal(char* string_to_print) {
    fprintf(fd_log_file_events, "%s", string_to_print);
    fflush(fd_log_file_events);
    printf("%s", string_to_print);
}

int new_message_receive(int **storage_fds[2], Message *message)
{
    int process_received_message_id =  receive_any(storage_fds, message);
    if(message->s_header.s_type == STARTED) messages_started_received++;
    else if(message->s_header.s_type == DONE) messages_done_received++;
    else if(message->s_header.s_type == CS_REPLY) return 1;
    if (process_self_id != 0 && message->s_header.s_type == CS_REQUEST)
    {
	    timestamp_t received_time = message->s_header.s_local_time;
	    if (process_flag_done || received_time < process_self_queue_time || (received_time == process_self_queue_time && process_received_message_id < process_self_id ))
	    {
		    process_own_time++;
		    message->s_header.s_type = CS_REPLY;
		    send(storage_fds, process_received_message_id, message);
	    }
	    else
	    {
	    	process_list_awaiting_reply[process_received_message_id] = 1;
	    }
    }

    return 0;
}

int request_cs(const void * self) {
    int counter = 0;
    message.s_header.s_type = CS_REQUEST;
    sprintf(message.s_payload, "");
    message.s_header.s_payload_len = 0;
    send_multicast((void*)self, &message);
    process_self_queue_time = get_lamport_time();
    while (counter < process_amount - 1) counter = new_message_receive((int***)self, &message) == 1 ? counter + 1 : counter;
    return 0;
}

int release_cs(const void * self) {
    message.s_header.s_type = CS_REPLY;
    sprintf(message.s_payload, "");
    message.s_header.s_payload_len = 0;
	for(short i = 1; i <= process_amount; i++) {
	    if (process_list_awaiting_reply[i])
	    {
	        process_own_time++;
	        send((void*)self, i, &message);
	        process_list_awaiting_reply[i] = 0;
	    }
	}
    return 0;
}

int matrix_flat_get(int i, int j, int k) {return k + 2*j + i*2*(process_amount+1);}

int main(int argc, char** argv)
{
    int offset = 2;
    if (strcmp("--mutexl", argv[1]) == 0)
    {
        offset++;
        process_mutex_activated = 1;
    }
    process_amount = atoi(argv[offset]);
    process_list_awaiting_reply = calloc(process_amount, sizeof(short));

    pid_t process_pid_main, process_pid_spawned;
    int storage_fds[process_amount + 1][process_amount+1][2];
    fd_log_file_events = fopen(events_log, "a");
    fd_log_file_pipe_descrs = fopen(pipes_log, "a");
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
            message.s_header.s_magic = MESSAGE_MAGIC;
            message.s_header.s_type = STARTED;
            sprintf(message.s_payload, log_started_fmt, get_lamport_time(), process_self_id, getpid(), process_pid_main, 0);
            message.s_header.s_payload_len = strlen(message.s_payload);
            send_multicast(storage_fds, &message);
            print_to_file_and_terminal(message.s_payload);
            while(messages_started_received < process_amount -1) new_message_receive((int***)storage_fds, &message);
            sprintf(buffer_temp, log_received_all_started_fmt, get_lamport_time(), process_self_id);
            print_to_file_and_terminal(buffer_temp);

            for(int i = 1; i <= 5 * process_self_id; i++)
            {
                if (process_mutex_activated)
                {
                    request_cs(storage_fds);
                }
                sprintf(buffer_temp, log_loop_operation_fmt, process_self_id, i, 5 * process_self_id);
                print(buffer_temp);
                if (process_mutex_activated) {
                    release_cs(storage_fds);
                }
            }
	        process_flag_done = 1;

            message.s_header.s_type = DONE;
            sprintf (message.s_payload, log_done_fmt, get_lamport_time(), process_self_id, 0);
            message.s_header.s_payload_len = strlen(message.s_payload);
            send_multicast(storage_fds, &message);
            print_to_file_and_terminal(message.s_payload);
            while(messages_done_received < process_amount -1) new_message_receive((int***)storage_fds, &message);
            sprintf(buffer_temp, log_received_all_done_fmt, get_lamport_time(), process_self_id);
            print_to_file_and_terminal(buffer_temp);

            exit(0);
        }
    }


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

    sprintf(buffer_temp, log_received_all_started_fmt, get_lamport_time(), 0);
    print_to_file_and_terminal(buffer_temp);

    while(messages_done_received < process_amount) new_message_receive((int***)storage_fds, &message);

    sprintf(buffer_temp, log_received_all_done_fmt, get_lamport_time(), 0);
    print_to_file_and_terminal(buffer_temp);

    while(process_child_stopped_count < process_amount)
    {
        wait(NULL);
        process_child_stopped_count++;
    }
    return 0;
}


