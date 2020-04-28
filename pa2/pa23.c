#include "pa23.h"

FILE *fd_log_file_events, *fd_log_file_pipe_descrs;
TransferOrder child_process_transfer_order;
BalanceHistory child_process_balance_history;
balance_t balance = 0;
char buffer_temp[MAX_MESSAGE_LEN];
int process_self_id, *process_balance;
short process_amount, messages_started_received = 0, messages_done_received = 0, process_stop = 0;

void print_to_file_and_terminal(char* string_to_print) {
    fprintf(fd_log_file_events, "%s", string_to_print);
    fflush(fd_log_file_events);
    printf("%s", string_to_print);
}

void parse_transfer_request(void *self, Message *message)
{
    timestamp_t own_process_time = get_physical_time();
    BalanceState *state;
    memcpy(&child_process_transfer_order, message->s_payload, sizeof(TransferOrder));
    int amount = child_process_transfer_order.s_amount, src_id = child_process_transfer_order.s_src;
    for(int i = child_process_balance_history.s_history_len; i < own_process_time; i++)
    {
        state = &(child_process_balance_history.s_history[i]);
        state->s_time = i;
        state->s_balance_pending_in = 0;
        state->s_balance = balance;
    }
    balance += (src_id == process_self_id ? -amount : amount);
    state = &(child_process_balance_history.s_history[own_process_time]);
    state->s_balance = balance;
    state->s_time = own_process_time;
    state->s_balance_pending_in = 0;
    child_process_balance_history.s_history_len = own_process_time + 1;
    if (src_id == process_self_id) {
        send(self, child_process_transfer_order.s_dst, message);
        sprintf(buffer_temp, log_transfer_out_fmt, get_physical_time(),process_self_id,amount, child_process_transfer_order.s_dst);
    } else {
        message->s_header.s_type = ACK;
        message->s_header.s_payload_len = 0;
        send(self, 0, message);
        sprintf(buffer_temp, log_transfer_in_fmt, get_physical_time(),process_self_id,amount, src_id);
    }
    print_to_file_and_terminal(buffer_temp);
}

void new_message_receive(int **storage_fds[2], Message *message)
{
    receive_any(storage_fds, message);
    if(message->s_header.s_type == STARTED) messages_started_received++;
    else if(message->s_header.s_type == DONE) messages_done_received++;
    else if(message->s_header.s_type == STOP) process_stop = 1;
    else if(message->s_header.s_type == TRANSFER) parse_transfer_request(storage_fds, message);
}

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    child_process_transfer_order.s_src = src;
    child_process_transfer_order.s_amount = amount;
    child_process_transfer_order.s_dst = dst;
    Message message;
    message.s_header.s_magic = MESSAGE_MAGIC;
    message.s_header.s_type = TRANSFER;
    memcpy(message.s_payload, &child_process_transfer_order, sizeof(TransferOrder));
    message.s_header.s_payload_len = sizeof(TransferOrder);
    send(parent_data, src, &message);
    do {
        new_message_receive(parent_data, &message);
        if(message.s_header.s_type == ACK) break;
    } while(1);
}

void get_child_process_history(AllHistory * history, Message *message, void *self){
	receive_any(self, message);
	memcpy(history->s_history + history->s_history_len, message->s_payload, sizeof(local_id) + sizeof(uint8_t));
	memcpy(history->s_history[history->s_history_len].s_history, message->s_payload + sizeof(local_id) + sizeof(uint8_t),
	        history->s_history[history->s_history_len].s_history_len * sizeof(BalanceHistory));
	
	history->s_history_len++;
}

int matrix_flat_get(int i, int j, int k) {return k + 2*j + i*2*(process_amount+1);}

int main(int argc, char** argv)
{
    process_amount = atoi(argv[2]);
    process_balance = calloc(process_amount, sizeof(int));
    for(int i = 3;i<process_amount+3;i++)
    {
        process_balance[i-3] = atoi(argv[i]);
    }

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
            balance = process_balance[i -1];
            child_process_balance_history.s_id = i;
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
            sprintf(message.s_payload, log_started_fmt, get_physical_time(), process_self_id, getpid(), process_pid_main, balance);
            message.s_header.s_payload_len = strlen(message.s_payload);
            send_multicast(storage_fds, &message);
            print_to_file_and_terminal(message.s_payload);

            while(messages_started_received < process_amount -1) new_message_receive((int***)storage_fds, &message);
            sprintf(buffer_temp, log_received_all_started_fmt, get_physical_time(), process_self_id);
            print_to_file_and_terminal(buffer_temp);

            while(process_stop != 1) new_message_receive((int***)storage_fds, &message);
            message.s_header.s_type = DONE;
            sprintf (message.s_payload, log_done_fmt, get_physical_time(), process_self_id, balance);
            message.s_header.s_payload_len = strlen(message.s_payload);
            send_multicast(storage_fds, &message);
            print_to_file_and_terminal(message.s_payload);

            while(messages_done_received < process_amount -1) new_message_receive((int***)storage_fds, &message);
            sprintf(buffer_temp, log_received_all_done_fmt, get_physical_time(), process_self_id);
            print_to_file_and_terminal(buffer_temp);

            timestamp_t own_process_time = get_physical_time();
            BalanceState *state;
            for(int i = child_process_balance_history.s_history_len; i <= own_process_time; i++)
            {
                state = &(child_process_balance_history.s_history[i]);
                state->s_time = i;
                state->s_balance_pending_in = 0;
                state->s_balance = balance;
            }
            child_process_balance_history.s_history_len = own_process_time + 1;

            message.s_header.s_type = BALANCE_HISTORY;
            size_t message_length = sizeof(local_id) + sizeof(uint8_t) + sizeof(BalanceState) * child_process_balance_history.s_history_len;
            memcpy(message.s_payload, &child_process_balance_history, message_length);
            message.s_header.s_payload_len = message_length;
            send(storage_fds, 0, &message);
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
    AllHistory process_transfer_history;
    int process_child_stopped_count = 0;
    while(messages_started_received < process_amount) new_message_receive((int***)storage_fds, &message);

    sprintf(buffer_temp, log_received_all_started_fmt, get_physical_time(), 0);
    print_to_file_and_terminal(buffer_temp);

    bank_robbery(storage_fds, process_amount);

    message.s_header.s_type = STOP;
    message.s_header.s_payload_len = 0;

    send_multicast(storage_fds, &message);

    while(messages_done_received < process_amount) new_message_receive((int***)storage_fds, &message);

    sprintf(buffer_temp, log_received_all_done_fmt, get_physical_time(), 0);
    print_to_file_and_terminal(buffer_temp);

    while(process_transfer_history.s_history_len < process_amount) get_child_process_history(&process_transfer_history, &message, storage_fds);
    print_history(&process_transfer_history);

    while(process_child_stopped_count < process_amount)
    {
        wait(NULL);
        process_child_stopped_count++;
    }
    return 0;
}


