#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#define B_SIZE 1024
#define C_NUM 200
#define GROUP_NUM 10
#define GROUP_MEM_NUM 20
#define SUCCESS 1
#define FAIL 0
int dueler_id=-1;
int password = 0;
int group[GROUP_NUM][GROUP_MEM_NUM]={0};
int password_arr[GROUP_MEM_NUM]={0};
int enc_key_array[GROUP_NUM]={0};
static _Atomic unsigned int client_count = 0; //because of thread can do both write and read (uninterruptible)
static int user_id = 1; //starting user id user id
static char topic[B_SIZE/8];// size of 128
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; // race free while multiple client using same resource
pthread_mutex_t topic_mutex = PTHREAD_MUTEX_INITIALIZER;  // race free while multiple topic using same resource
typedef struct {//Client structure  UPDATED
    struct sockaddr_in addr;     //Client remote address
    int connfd;                 //Connection file descriptor
    int user_id;               //User identifier
    int group_id;		      //Group 0 is lobby, updates when group change
    char name[50];           //Client name-nick
    int msgtogrp;           //Flag of message range
    float rep;             //Reputation Point
    float health;         //Health of the User
    float attack_power;  //Power of the User
    float luck;         //Luck of the user
} client_t;

client_t *client_array[C_NUM];
char *str_duplicate(const char *str);
void ceaser_enc(char arr[],int key);
char *ceaser_dec(char arr[],int key);
void q_adr(client_t *client);
void q_dlt(int user_id);
void forward_message(char *str, int user_id);
void group_forward(char *str, int user_id, int group_id);
void send_msg_all(char *str);
void forward_message_self(const char *str, int connfd);
void msg_from_sv_to_clt(char *str, int user_id);
void group_message(char *str,int group_id);
void active_client_list(int connfd, int group_id);
void active_group_list(int connfd);
void string_ender(char *str);
void print_client_addr(struct sockaddr_in addr);
void *client_handler(void *arg);
int create(int password,int user_id);
int join(int group_id,int password,int user_id);
int grp_mem(int group_id);
void disconnect(int group_id, int user_id );
int toggler(int input);
int fight(int duelist_1,int duelist_2);

int main(int argc, char *argv[]){
    int listenfd = 0, connfd = 0;
    int i;
    struct sockaddr_in serv_addr;
    struct sockaddr_in client_address;
    pthread_t thread_id;
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }
    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);//storing socket
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//default IP
    serv_addr.sin_port = htons(atoi(argv[1]));// for giving port number manually


    signal(SIGPIPE, SIG_IGN);// for early disconnections(Ignore pipe signals)


    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {//Binding sockets
        perror("Socket binding error");
        return FAIL;
    }
    if (listen(listenfd, 10) < 0) {//Listening sockets
        perror("Socket listen error");
        return FAIL;
    }

    printf("Server has been started\n");
    while (1) {//Accepting clients
        socklen_t client_address_len = sizeof(client_address);
        connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_address_len);
        if ((client_count + 1) == C_NUM) {//Checking the client capacity of the server
            printf(" Maximum number of clients has been connected\n");
            printf(" Rejection due to out of capacity!! ");
            print_client_addr(client_address);
            printf("\n");
            close(connfd);
            continue;
        }
		client_t *client_pointer = (client_t *)malloc(sizeof(client_t));//For store settings of clients (dynamic allocation)
        client_pointer->addr = client_address;
        client_pointer->connfd = connfd;
		for (i = 0; i < C_NUM; i++) {//determining user id numbers whenever a new client has connected
		    if (client_array[i] == 0) {//if corresponding space is empty
		        user_id = i+1; // then make user id as i +1 which is 1 more of client array number 0->1 or 1->2 (there is no 0th client)
		        break;
		    }
		}
        srand(time(NULL));
		client_pointer->user_id = user_id;// giving userid to the client struct for gathering all the info at one place
        client_pointer->group_id = 0;// at first everyone at lobby group 0
        client_pointer->msgtogrp = 0;// at first users send their message to everyone encrypted or not
        client_pointer->rep = 100.0;
        client_pointer->luck = rand()%10;
        client_pointer->health = 100.0 + client_pointer->luck;
        client_pointer->attack_power =10.0 + client_pointer->luck;
        sprintf(client_pointer->name, "%d", client_pointer->user_id); //Default client name is user id (until nickname)
        q_adr(client_pointer);// queing
        pthread_create(&thread_id, NULL, &client_handler, (void*)client_pointer); //forking
        sleep(1);// wait for connection do not close the server
    }
    return SUCCESS;
}

char *str_duplicate(const char *str) {//Storing a copy of the input OLD
    size_t size = strlen(str) + 1;
    char *ptr = malloc(size);
    if (ptr) {
        memcpy(ptr, str, size);
    }
    return ptr;
}

void ceaser_enc(char arr[],int key){//Ceaser cryption   NEW
	int i;
	for(i = 0; i < strlen(arr); i++)
	{
	    arr[i]= arr[i] + key;
	}
}

char *ceaser_dec(char arr[],int key){//Ceaser decryption    NEW
	int i;
	char *ptr = str_duplicate(arr);
	for(i = 0; i < strlen(arr); i++)
	{
	    ptr[i] = ptr[i] - key;
	}
	return ptr;
}

void q_adr(client_t *client){//Add client to queue when client connected OLD
    int i;
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++) {
        if (!client_array[i]) {
            client_array[i] = client;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void q_dlt(int user_id){//Removing client from the list if client quits OLD
    int i;
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++) {
        if (client_array[i]) {
            if (client_array[i]->user_id == user_id) {
                client_array[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void forward_message(char *str, int user_id){//Forwarding messages to other clients UPDATED
	int key, i;
	char buff_out[B_SIZE];
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++) {
		buff_out[0] = '\0';//initializing
        if (client_array[i]) {
            if (client_array[i]->user_id != user_id) {//until equals self
            	key=enc_key_array[client_array[i]->group_id];//reaching session key which is special to group
            	snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", client_array[user_id-1]->name, ceaser_dec(str, key));
                if (write(client_array[i]->connfd, buff_out, strlen(buff_out)) < 0) {
                    perror("Forwarding error");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void group_forward(char *str, int user_id, int group_id){//     NEW
	int key, i;
	char buff_out[B_SIZE];
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++) {
		buff_out[0] = '\0';//initializing
        if (client_array[i]) {
            if ((client_array[i]->user_id != user_id) && (client_array[i]->group_id== group_id)) {//until equals self
            	key=enc_key_array[client_array[i]->group_id];
            	snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", client_array[user_id-1]->name, ceaser_dec(str, key));
                if (write(client_array[i]->connfd, buff_out, strlen(buff_out)) < 0) {
                    perror("Forwarding error");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_msg_all(char *str){//Send message to all clients  OLD
	int i;
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i <C_NUM; i++){
        if (client_array[i]) {
            if (write(client_array[i]->connfd, str, strlen(str)) < 0) {
                perror("Sending message to all clients error");
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void forward_message_self(const char *str, int connfd){//Send message to self   OLD
    if (write(connfd, str, strlen(str)) < 0) {
        perror("Forwarding message to self error");
        exit(-1);
    }
}

void msg_from_sv_to_clt(char *str, int user_id){//Sending message to client OLD
	int i;
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++){
        if (client_array[i]) {
            if (client_array[i]->user_id == user_id) {
                if (write(client_array[i]->connfd, str, strlen(str))<0) {
                    perror("Message from server to client error ");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void group_message(char *str,int group_id){//Sending message to group   NEW
	int i;
    pthread_mutex_lock(&clients_mutex);
    for (i = 0; i < C_NUM; i++){
        if (client_array[i]) {
            if (client_array[i]->group_id == group_id) {
                if (write(client_array[i]->connfd, str, strlen(str))<0) {
                    perror("Group message error");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void active_client_list(int connfd, int group_id){//Send list of active clients UPDATED
    char s[128];
	int i;
    pthread_mutex_lock(&clients_mutex);//for sync between clients
    if (group_id == -1){//for everyone
		for (i = 0; i < C_NUM; i++){
			if (client_array[i]) {
			    sprintf(s, " [%d] %s\r\n", client_array[i]->user_id, client_array[i]->name);
			    forward_message_self(s, connfd);
	        }
		}
	}
	else if (group_id == 0) {//for request of group member list from lobby
        forward_message_self("You are not in a group.\r\nUse </list> command to see all users.\r\n", connfd);
	}
	else { // for seeing group members in same group
	int counter = 0;
		for (i = 0; i < GROUP_MEM_NUM; i++) {
			if (group[group_id][i]){
				counter++;//active group member
				if(i == 0){//If member is first member of a group(ADMIN)
					sprintf(s, "[ADMIN]  [%d] %s\r\n", group[group_id][i], client_array[group[group_id][i]-1]->name);
				    forward_message_self(s, connfd);
				}
				else {// Other users(MEMBERS)
					sprintf(s, "[MEMBER] [%d] %s\r\n", group[group_id][i], client_array[group[group_id][i]-1]->name);
				    forward_message_self(s, connfd);
				}
		 	}
		}
		sprintf(s, "Total number of member this group is %d\r\n", counter);
	    forward_message_self(s, connfd);
	}
    pthread_mutex_unlock(&clients_mutex);//unlock the thread
}

void active_group_list(int connfd){//Send list of active groups NEW
    char s[64];
	int i, counter = 0;
	pthread_mutex_lock(&clients_mutex);
	for (i = 0; i < GROUP_NUM; i++){
	    if (group[i][0] != 0) {
	    	counter++;
	        sprintf(s, "Group[%d] is Active\r\n", i);
	        forward_message_self(s, connfd);
	    }
	}
	if (counter == 0){
        forward_message_self("There is no Active group. To create one </create>\r\n", connfd);
	}
	pthread_mutex_unlock(&clients_mutex);
}

void string_ender(char *str){//String ender(adding \0 while necessary ) OLD
    while (*str != '\0') {// scan until last element of string
        if (*str == '\n' || *str == '\r') { //if newline then close the string (/0)(replace)
            *str = '\0';
        }
        str++;
    }
}

void print_client_addr(struct sockaddr_in addr){//Printing the ip address by shifting 8 bit recursively OLD
    printf("%d.%d.%d.%d",
    addr.sin_addr.s_addr & 0xff,
    (addr.sin_addr.s_addr & 0xff00) >> 8,
    (addr.sin_addr.s_addr & 0xff0000) >> 16,
    (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

void *client_handler(void *arg){//Providing communication  between all server and clients
    char buff_out[B_SIZE];
    char input_buffer[B_SIZE / 2];
    int rlen;
    int i;

    client_count++;
    client_t *client_pointer = (client_t *)arg;

    printf("Client accepted: User %d ", client_pointer->user_id);
    print_client_addr(client_pointer->addr);
    printf("\n");
    send_msg_all("\nA new User has joined\r\n");

    pthread_mutex_lock(&topic_mutex);
    if (strlen(topic)) {
        buff_out[0] = '\0';
        sprintf(buff_out, " topic: %s\r\n", topic);
        forward_message_self(buff_out, client_pointer->connfd);
    }
    pthread_mutex_unlock(&topic_mutex);

    forward_message_self("You can write </help> for available commands  \n", client_pointer->connfd);


    while ((rlen = read(client_pointer->connfd, input_buffer, sizeof(input_buffer) - 1)) > 0) {//Getting input from clients continuously
        input_buffer[rlen] = '\0'; //making array to string
        buff_out[0] = '\0';// initilizing buffer output
        string_ender(input_buffer);


        if (strlen(input_buffer)==0) {//Now infinite newlines does not corrupt the code(empty buffers)
            continue;
        }


        if (input_buffer[0] == '/') {//Command handler
            char *command, *param;
            command = strtok(input_buffer," ");
            if (strcmp(command, "/quit") == 0) {
                if(client_pointer->group_id != 0){
                    sprintf(buff_out, "%s has been disconnected\r\n", client_pointer->name);
                    group_message(buff_out,client_pointer->group_id);
                    disconnect(client_pointer->group_id, client_pointer->user_id);
                }
                break;
            } else if (strcmp(command, "/info") == 0) {
				sprintf(buff_out, "User id = %d\r\nName = %s\r\nGroup = %d\r\nMessage to group only = %d\r\nReputation points = %.2f\r\nAttack power = %.2f\r\nHealth= %.2f\r\n",
				client_pointer->user_id, client_pointer->name,  client_pointer->group_id,client_pointer->msgtogrp,
				client_pointer->rep,client_pointer->attack_power, client_pointer->health);
		    	forward_message_self(buff_out, client_pointer->connfd);

            } else if (strcmp(command, "/topic") == 0) {
                param = strtok(NULL, " ");
                int groupid=client_pointer->group_id;
                if (param) {
                    pthread_mutex_lock(&topic_mutex);
                    topic[0] = '\0';
                    while (param != NULL) {
                        strcat(topic, param);
                        strcat(topic, " ");
                        param = strtok(NULL, " ");
                    }
                    pthread_mutex_unlock(&topic_mutex);
                    sprintf(buff_out, "Group's topic changed to: %s \r\n", topic);
                    group_message(buff_out,groupid);
                } else {
                    forward_message_self(" topic cannot be null\r\n", client_pointer->connfd);
                }
            } else if (strcmp(command, "/nick") == 0) {
                param = strtok(NULL, " ");
                if (param) {
                    char *nick = str_duplicate(client_pointer->name);// for storing previous nick
                    if (!nick) {
                        perror("Nick has not been set error");
                        continue;
                    }
                    strncpy(client_pointer->name, param, sizeof(client_pointer->name));
                    client_pointer->name[sizeof(client_pointer->name)-1] = '\0';
                    sprintf(buff_out, " %s is now known as %s\r\n", nick, client_pointer->name);
                    free(nick); // reducing ram usage since no need to store previous nick
                    send_msg_all(buff_out);
                } else {
                    forward_message_self(" invalid nick \n", client_pointer->connfd);
                }
            } else if (strcmp(command, "/msg") == 0) {
                param = strtok(NULL, " ");
                if (param) {
                    int user_id = atoi(param);
                    param = strtok(NULL, " ");
                    if (param) {
                        sprintf(buff_out, "[PM][From User %s]", client_pointer->name);
                        while (param != NULL) {
                            strcat(buff_out, " ");
                            strcat(buff_out, param);
                            param = strtok(NULL, " ");
                        }
                        strcat(buff_out, "\r\n");
                        msg_from_sv_to_clt(buff_out, user_id);
                    } else {
                        forward_message_self(" message cannot be null\r\n", client_pointer->connfd);
                    }
                } else {
                    forward_message_self(" receiver cannot be null\r\n", client_pointer->connfd);
                }
            } else if(strcmp(command, "/list" )==0) {
                sprintf(buff_out, " Connected Client number is: %d\r\n", client_count);
                forward_message_self(buff_out, client_pointer->connfd);
                active_client_list(client_pointer->connfd, -1);
            } else if(strcmp(command, "/create" )==0){
                int group_id;
                int flag = 0;
                param = strtok(NULL, " ");
                if (param){
                	if (client_pointer->group_id != 0) {
                		disconnect(client_pointer->group_id, client_pointer->user_id);
                	}
		            while (param != NULL) {
		                strcat(buff_out, param);
		                param = strtok(NULL, " ");
		            }
		            for (i = 0; buff_out[i] != '\0'; i++){
		            	if ( ('0' <= buff_out[i] && buff_out[i] <= '9') == 0 ){
		            		flag = 1;
		            	}
		            }
		            if (flag == 1){
		            	forward_message_self("Your password should contain only numbers\r\n", client_pointer->connfd);
		            }
		            else{
				        group_id = create(atoi(buff_out),client_pointer->user_id);
				        if(group_id == -1){
                            forward_message_self("Maximum group number has been reached\r\n", client_pointer->connfd);
				        }
				        else{
                            group[group_id][0] = client_pointer->user_id;
                            printf("Group %d has been created by User:%d\r\n",group_id, group[group_id][0]);
                            strcat(buff_out, "\r\n");
                            forward_message_self("You are admin.\nYour group password is: ", client_pointer->connfd);
                            forward_message_self(buff_out, client_pointer->connfd);
                            forward_message_self("Your group number is: ", client_pointer->connfd);
                            sprintf(buff_out,"%d\r\n",group_id);
                            forward_message_self(buff_out, client_pointer->connfd);
                            client_pointer->group_id = group_id;
				        }
		            }
	            } else {
	            	forward_message_self("Please Enter a password to create a group \r\n", client_pointer->connfd);
	            }
            } else if(strcmp(command,"/join")==0){
                int j = 0;
                param = strtok(NULL, " ");
                if (param) {
                    int j_group_id = atoi(param);
                    param = strtok(NULL, " ");
                    if (param){
			            int j_group_pw = atoi(param);
			            if(j_group_id == client_pointer->group_id ){
                            forward_message_self("You are already in this group\r\n", client_pointer->connfd);
			            }
						else{

				            j=join(j_group_id,j_group_pw,client_pointer->user_id);

				            if(j == 0){
				            	forward_message_self("Your password is wrong\r\n", client_pointer->connfd);
				            }
				            else if(j == 1){
						        disconnect(client_pointer->group_id, client_pointer->user_id);
						        forward_message_self("Your group password is correct\n Welcome\r\n", client_pointer->connfd);
						        client_pointer->group_id = j_group_id;
                                sprintf(buff_out, "%s has been joined\r\n", client_pointer->name);
                                group_message(buff_out,client_pointer->group_id);
				            }
				            else if (j == -1){
				            	forward_message_self("There is no group with that number\r\n", client_pointer->connfd);
				            }
				            else if (j == 2){
								forward_message_self("This group is full \r\n", client_pointer->connfd);
				            }
						}
	                } else {
	                	forward_message_self("Please Enter a password to join a group\r\n", client_pointer->connfd);
                	}
                }
            } else if(strcmp(command,"/dc")==0){
            	if(client_pointer->group_id != 0){
            		sprintf(buff_out, "%s has been disconnected\r\n", client_pointer->name);
                    group_message(buff_out,client_pointer->group_id);
                    disconnect(client_pointer->group_id, client_pointer->user_id);
            		client_pointer->group_id = 0;
            		forward_message_self(" You are in lobby\r\n", client_pointer->connfd);
				}
				else{
					forward_message_self(" You cant disconnect from lobby. Try </quit> command.\r\n", client_pointer->connfd);
				}
            } else if(strcmp(command,"/grp_list")==0){
            	active_group_list(client_pointer->connfd);
            } else if(strcmp(command,"/grp_mem")==0){
            	int group_id = client_pointer->group_id;
                active_client_list(client_pointer->connfd, group_id);
            }else if (strcmp(command, "/grp_m") == 0) {
                param = strtok(NULL, " ");
                if (param) {
                    sprintf(buff_out, "[Group_message][From User %s]", client_pointer->name);
                    while (param != NULL) {
                        strcat(buff_out, " ");
                        strcat(buff_out, param);
                        param = strtok(NULL, " ");
                    }
                    strcat(buff_out, "\r\n");
                    group_message(buff_out,client_pointer->group_id);
                } else {
                    forward_message_self(" message cannot be null\r\n", client_pointer->connfd);
                }
             }else if(strcmp(command,"/grp_a")==0){
            	client_pointer->msgtogrp=toggler(client_pointer->msgtogrp);
            }else if(strcmp(command,"/kick")==0){
            	int group_id = client_pointer->group_id;
            	if (client_pointer->user_id == group[group_id][0]){//if admin
		        	param = strtok(NULL, " ");
		            if (param) {
		            	int user_id = atoi(param);
		            	if(client_array[user_id-1]){//if client exist
				        	if(client_array[user_id-1]->group_id == group_id){//if client in the same group
				    			sprintf(buff_out, "%s has been disconnected\r\n", client_array[user_id-1]->name);
                                group_message(buff_out,client_pointer->group_id);
                                disconnect(group_id, user_id);
				    			client_array[user_id-1]->group_id = 0;
						    	sprintf(buff_out, " You have been kicked by [ADMIN] %s\r\n", client_pointer->name);
						    	msg_from_sv_to_clt(buff_out, user_id);
						    	sprintf(buff_out, " Member with user_id [%d] has been kicked.\r\n", user_id);
						    	forward_message_self(buff_out, client_pointer->connfd);
				        	}
				        	else {//if client does not in the same group
						    	sprintf(buff_out, " There is no member with user_id in this group [%d].\r\n", user_id);
				        		forward_message_self(buff_out, client_pointer->connfd);
				        	}
			        	}
			        	else {//if client does not exist
							sprintf(buff_out, " There is no member with user_id [%d].\r\n", user_id);
				    		forward_message_self(buff_out, client_pointer->connfd);
			        	}
		            } else {//if user_id is not entered
		                forward_message_self(" Enter user_id of the member you want to kick.\r\n", client_pointer->connfd);
		            }
            	} else {//if Not admin
		        	forward_message_self("You are not the Admin.\r\n", client_pointer->connfd);
	        	}
            } else if(strcmp(command,"/dice")==0){
            	param = strtok(NULL, " ");
            	if(param){
                    int face = atoi(param);
                    float half = 0;
                    srand(time(NULL));
                    int die = (rand() % face) + 1;
                    sprintf(buff_out, " You rolled %d.\r\n", die);
                    forward_message_self(buff_out, client_pointer->connfd);
                    half = face;
                    if(die < half/2.0){
                        client_pointer->rep = client_pointer->rep - ((half/2.0)-die);
                        sprintf(buff_out, " You lose %.2f rep points.\r\n",((half/2.0)-die));
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else if(die >= half/2.0){
                        client_pointer->rep = client_pointer->rep + (die-(half/2.0));
                        sprintf(buff_out, " You win %.2f rep points.\r\n",(die-(half/2.0)));
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
            	}
            	else{
                    forward_message_self("Please give face number of a die! Usage: /dice <face_number>.\r\n", client_pointer->connfd);
            	}


            } else if(strcmp(command, "/duel" )==0) {
            	param = strtok(NULL, " ");
            	if(param){
                    int duel_user_id = atoi(param);
                    if((client_array[duel_user_id-1]->health > 0) && (client_array[duel_user_id-1]->rep > 0) && (duel_user_id !=client_pointer->user_id) && (client_pointer->health > 0) && (client_pointer->rep > 0)){
                        sprintf(buff_out, "%s invited you to duel. If you accept please write </accept %d>\r\n", client_pointer->name,client_pointer->user_id);
                        msg_from_sv_to_clt(buff_out,duel_user_id);
                        dueler_id=client_pointer->user_id;
                    }
                    else if(duel_user_id == client_pointer->user_id){
                        forward_message_self("You cant invite yourself to the duel :D.\r\n", client_pointer->connfd);
                    }
                    else if((client_pointer->health <= 0) && (client_pointer->rep <= 0)){
                        forward_message_self("Insufficient health or rep points to invite someone to duel.\r\n", client_pointer->connfd);
                    }
                    else{
                        forward_message_self("Given duelist has no health or rep points.\r\n", client_pointer->connfd);
                    }
            	}
                else{
                    forward_message_self("Please give duelist number ! Usage: /duel <duelist_id_number>.\r\n", client_pointer->connfd);
                }


            } else if(strcmp(command, "/accept" )==0) {
            	param = strtok(NULL, " ");
            	if(param){
                    int temp_point1=0;
                    int temp_point2=0;
                    int duel_user_id = atoi(param);
                    int winner=0;
                    if((duel_user_id == dueler_id) && (dueler_id !=-1)){
                        temp_point1 += client_array[duel_user_id-1]->rep;
                        client_array[duel_user_id-1]->rep = client_array[duel_user_id-1]->rep - temp_point1;
                        temp_point2 += client_pointer->rep;
                        client_pointer->rep = client_pointer->rep - temp_point2;
                        sprintf(buff_out, "%s accepted your duel request.The combat will started shortly.\r\n", client_pointer->name);
                        msg_from_sv_to_clt(buff_out,duel_user_id);
                        sprintf(buff_out, "The combat will started shortly.\r\n");
                        msg_from_sv_to_clt(buff_out, client_pointer->user_id);
                        winner=fight(duel_user_id,client_pointer->user_id);
                        printf("Winner is %d\r\n",winner);
                        if(winner == 1){
                            client_array[duel_user_id-1]->rep = temp_point1 + temp_point2;
                            sprintf(buff_out, "You win %d rep points.\r\n",  temp_point2);
                            msg_from_sv_to_clt(buff_out,duel_user_id);
                            sprintf(buff_out, "You lose %d rep points.\r\n",temp_point1);
                            msg_from_sv_to_clt(buff_out,client_pointer->user_id);
                            if(client_pointer->health <= 0){
                                sprintf(buff_out, "%s has been disconnected\r\n", client_pointer->name);
                                group_message(buff_out,client_pointer->group_id);
                                disconnect(client_pointer->group_id,client_pointer->user_id);
                                client_pointer->group_id = 0;
                                msg_from_sv_to_clt("You have been disconnected from the group due to death.\r\n",client_pointer->user_id);
                            }
                        }
                        else if(winner == 2 ){
                            client_pointer->rep = temp_point1 + temp_point2;
                            sprintf(buff_out, "You lose %d rep points.\r\n", temp_point2);
                            msg_from_sv_to_clt(buff_out,duel_user_id);
                            sprintf(buff_out, "You win %d rep points.\r\n", temp_point1);
                            msg_from_sv_to_clt(buff_out,client_pointer->user_id);
                            if(client_array[duel_user_id-1]->health<=0){
                                sprintf(buff_out, "%s has been disconnected\r\n", client_array[duel_user_id-1]->name);
                                group_message(buff_out,client_array[duel_user_id-1]->group_id);
                                disconnect(client_array[duel_user_id-1]->group_id,duel_user_id);
                                client_array[duel_user_id-1]->group_id = 0;
                                msg_from_sv_to_clt("You have been disconnected from the group due to death.\r\n",client_array[duel_user_id-1]->user_id);
                            }
                        }
                        dueler_id=-1;
                    }
                    else{
                        forward_message_self("Given duelist id wrong or does not exists Usage: /accept <duelist_id_number>.\r\n", client_pointer->connfd);                        

                    }
            	}
                else{
                    forward_message_self("Please give duelist number ! Usage: /accept <duelist_id_number>.\r\n", client_pointer->connfd);
                }

            } else if(strcmp(command,"/hp")==0){
                param = strtok(NULL, " ");
                if(param){
                    float req_hp = atoi(param);
                    if (client_pointer->rep >= req_hp)
                    {
                        client_pointer->rep= client_pointer->rep - req_hp;
                        client_pointer->health= client_pointer->health + req_hp;
                        sprintf(buff_out,"You have traded %.2f health with %.2f rep points. Your current rep points: %.2f and health is: %.2f \r\n", req_hp,req_hp,client_pointer->rep,client_pointer->health);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else{
                        forward_message_self("You do not have enough rep point for requested health.\r\n", client_pointer->connfd);
                    }
                    
                }
                else{
                    forward_message_self("Please give requested health point usage: /hp <requested health point> .\r\n", client_pointer->connfd);
                }
            }else if(strcmp(command,"/attack")==0){
                param = strtok(NULL, " ");
                if(param){
                    float req_atk = atoi(param)/10;

                    if (client_pointer->rep >= req_atk)
                    {
                        client_pointer->rep= client_pointer->rep - 10*req_atk;
                        client_pointer->attack_power= client_pointer->attack_power + req_atk;
                        sprintf(buff_out,"You have traded %.2f attack point with %.2f rep points. Your current rep points: %.2f and attack point is: %.2f \r\n", req_atk,10*req_atk,client_pointer->rep,client_pointer->attack_power);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else{
                        forward_message_self("You do not have enough rep point for requested attack point.\r\n", client_pointer->connfd);
                    }
                    
                }
                else{
                    forward_message_self("Please give requested attack point usage: /attack <requested attack point> .\r\n", client_pointer->connfd);
                }
            }else if(strcmp(command,"/rps")==0){
                char rps[3]={'r','p','s'};
            	param = strtok(NULL, " ");
            	if(param){
                    char chosen = param[0];
                    srand(time(NULL));
                    char rslt = rps[rand() % 3];
                    if(rslt == chosen){
                        sprintf(buff_out, " [Tie!] You choose %c. Server choose %c\r\n", chosen,rslt);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else if(rslt=='r' && chosen == 's'){
                        client_pointer->rep = client_pointer->rep - 5.0;
                        sprintf(buff_out, " [You lose!] You choose %c. Server choose %c. You lost 5 rep points\r\n", chosen,rslt);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else if(rslt=='p' && chosen == 'r'){
                        client_pointer->rep = client_pointer->rep - 5.0;
                        sprintf(buff_out, " [You lose!] You choose %c. Server choose %c. You lost 5 rep points\r\n", chosen,rslt);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else if(rslt=='s' && chosen == 'p'){
                        client_pointer->rep = client_pointer->rep - 5.0;
                        sprintf(buff_out, " [You lose!] You choose %c. Server choose %c. You lost 5 rep points\r\n", chosen,rslt);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
                    else{
                        client_pointer->rep = client_pointer->rep + 5.0;
                        sprintf(buff_out, " [You win!] You choose %c. Server choose %c. You won 5 rep points\r\n", chosen,rslt);
                        forward_message_self(buff_out, client_pointer->connfd);
                    }
            	}
            	else{
                    forward_message_self("Please give your choice ! Usage: /dice <r or p or s>.\r\n", client_pointer->connfd);
            	}

            } else if (strcmp(command, "/help") == 0) {
                strcat(buff_out, "/quit ===========> Exit from server\r\n");
                strcat(buff_out, "/info ===========> Ask your info to server\r\n");
                strcat(buff_out, "/topic ==========> <topic_msg> Create a topic\r\n");
                strcat(buff_out, "/nick ===========> <name> Create a nickname\r\n");
                strcat(buff_out, "/list ===========> Show connected clients\r\n");
                strcat(buff_out, "/msg ============> <user_id>(number) <msg> Send private message\r\n");
                strcat(buff_out, "/create =========> <password> Create a group with password(number only)\r\n");
                strcat(buff_out, "/join ===========> <group_id> <password> Join a group with password(number only)\r\n");
                strcat(buff_out, "/dc =============> disconnected from group\r\n");
                strcat(buff_out, "/grp_mem ========> Show connected clients in the group members\r\n");
                strcat(buff_out, "/grp_list =======> Show active groups\r\n");
                strcat(buff_out, "/grp_m ==========> Send one message to the group\r\n");
                strcat(buff_out, "/grp_a ==========> [default=everyone]Toggles the destination of the messages Group or everyone\r\n");
                strcat(buff_out, "/kick ===========> <user_id>(number) Kicks a member of group (Admin command) \r\n");
                strcat(buff_out, "/dice ===========> <face_number> Rolls a die\r\n");
                strcat(buff_out, "/rps ===========> <choose>(r or p or s) Rock paper scissors\r\n");
				strcat(buff_out, "/duel ===========> <user_id> Invite to someone to Duel\r\n");
                strcat(buff_out, "/accept ===========> <user_id> Accept Duel request from user\r\n");
                strcat(buff_out, "/hp ===========>  <Requested health> Trading health points with rep points \r\n");
                strcat(buff_out, "/attack ===========>  <Requested attack points> Trading attack points with rep points \r\n");
                strcat(buff_out, "/help ===========> Show help\r\n");
                forward_message_self(buff_out, client_pointer->connfd);
              }
            else {
                forward_message_self(" Unknown command\r\nTo see commands use </help> \r\n", client_pointer->connfd);
            }
        } else {
        	srand(time(NULL));
			int key = rand() % 26;// random key generating between 0-26
			enc_key_array[client_pointer->group_id]=key;// storing that random key to decrypt ciphers for group members
            ceaser_enc(input_buffer,key);// encryption of message
            snprintf(buff_out, sizeof(buff_out), "%s", input_buffer);//put the ciphertext(input_buffer) into buff_out
            if(client_pointer->msgtogrp == 0){
				forward_message(buff_out, client_pointer->user_id);// forward message
			}
			else if(client_pointer->msgtogrp == 1){
				group_forward(buff_out, client_pointer->user_id, client_pointer->group_id);
			}
        }
    }


    sprintf(buff_out, "%s has left\r\n", client_pointer->name);//Close connection
    send_msg_all(buff_out);//notice all users who has been disconnected from the server
    close(client_pointer->connfd);//close the socket


    q_dlt(client_pointer->user_id);//Delete client from queue and yield thread
    printf(" User  %d has been disconnected with ip address: ", client_pointer->user_id);
    print_client_addr(client_pointer->addr);
    printf("\n");
    free(client_pointer);
    client_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int create(int password,int user_id){//Creating a Group with password       NEW
	int i,j;
	if(group[GROUP_NUM-1][0] != 0){
        return -1;
	}
	else{
        for(i=1;i<GROUP_NUM;i++){
            if (group[i][0] == 0){
                group[i][0] = user_id;
                password_arr[i] = password;
                printf("The Group %d has been created with %d\n",i,password);
                return i;
            }
        }
    }
}

int join(int group_id,int password,int user_id){//Joining a Group with a password   NEW
    int i;

	if(group[group_id][0] == 0){
        return -1;
    }
    else{
        if(password == password_arr[group_id]){
            if(group[group_id][GROUP_MEM_NUM] != 0){
            	return 2;
			}
			else{
				for(i=1;i<GROUP_MEM_NUM;i++){
                	if(group[group_id][i] == 0){
                    	group[group_id][i]=user_id;
                    	return 1;
                	}
            	}
        	}
		}
        else if(password != password_arr[group_id]){
            return 0;
        }
    }

}

void disconnect(int group_id, int user_id ){//Disconnecting from  a Group   NEW
	int i,j;
	int admin_check = 0;
	for (i = 0; i < GROUP_MEM_NUM, group[group_id][i] != 0; i++) {
		if(group[group_id][i] == user_id){
			if(i == 0)	admin_check = 1;
			for (; i < GROUP_MEM_NUM, group[group_id][i] != 0; i++){
				if (i == GROUP_MEM_NUM - 1) {
					group[group_id][i] = 0;
				}
				else {
					group[group_id][i] = group[group_id][i+1];
				}
			}
		}
	}
	if (admin_check == 1)
		msg_from_sv_to_clt("You are the Admin now. Have Fun.\r\n", group[group_id][0]);
}

int toggler(int input){//   NEW
	if(input == 1){
		input = 0;
	}
	else if(input == 0){
		input = 1;
	}
	return input;
}

int fight(int duelist_1,int duelist_2){//   NEW
    duelist_1= duelist_1 - 1;
    duelist_2= duelist_2 - 1;
    int turn;
    srand(time(NULL));
    while(client_array[duelist_1]->health > 0.0 && client_array[duelist_2]->health > 0.0){
        turn = (rand() % 2) + 1;
        if(turn == 2){
            client_array[duelist_1]->health=client_array[duelist_1]->health - client_array[duelist_2]->attack_power;
            client_array[duelist_2]->health=client_array[duelist_2]->health - client_array[duelist_1]->attack_power;
        }
        else if(turn == 1){
            client_array[duelist_2]->health=client_array[duelist_2]->health - client_array[duelist_1]->attack_power;
            client_array[duelist_1]->health=client_array[duelist_1]->health - client_array[duelist_2]->attack_power;
        }
    }
    if(client_array[duelist_1]->health <= 0.0){
        return 2;
    }
    else if(client_array[duelist_2]->health <= 0.0){
        return 1;
    }
    else {
        return 0;
    }
}