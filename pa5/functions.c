#include "functions.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int send(void * self, local_id dst, const Message * msg)
{
	((Message*)msg)->s_header.s_local_time = get_lamport_time();
	int *storage_fds = self;
	int length = msg->s_header.s_payload_len + sizeof(MessageHeader);
	return write(*(storage_fds + matrix_flat_get(process_self_id,dst,1)), msg, length) == -1 ? -1 : 0;
}

int send_multicast(void * self, const Message * msg)
{
	int result_message_sent;
	int *storage_fds = self;
	int i = 0;
	process_own_time++;
	while(i <= process_amount) {
		if(*(storage_fds + matrix_flat_get(process_self_id, i,1))!=-1)
		{
			result_message_sent = send(self, i, msg);
			if (result_message_sent == 0) i++;
		}
		else {
			i++;
		}
	}
	return result_message_sent;
}

int receive(void * self, local_id from, Message * msg)
{
	int *storage_fds = self;
	int payload_length;
	int offset = matrix_flat_get(from, process_self_id, 0);
	payload_length = read(*(storage_fds + offset), msg, sizeof(MessageHeader));
	if(payload_length == -1) {
		return -1;
	}
	if(payload_length > 0)
	{
		payload_length = read(*(storage_fds + offset), msg->s_payload, msg->s_header.s_payload_len);
		msg->s_payload[msg->s_header.s_payload_len] = 0;
        if (msg->s_header.s_local_time > get_lamport_time())process_own_time = msg->s_header.s_local_time;
        process_own_time = msg->s_header.s_local_time > get_lamport_time() ? msg->s_header.s_local_time + 1 : process_own_time + 1;
		return 0;
	}
	return -2;
}

int receive_any(void * self, Message * msg) {
	int *storage_fds = self, result_message_received;
	while(1)
	{
		for(int i = 0; i <= process_amount;i++)
		{
			if (*(storage_fds + matrix_flat_get(i,process_self_id,0)) != -1)
			{
				result_message_received = receive(self, i, msg);
				if (result_message_received == 0) return i;
			}
		}
		usleep(10);
	}
}


