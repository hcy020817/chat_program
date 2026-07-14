//표준 헤더(운영체제 상관없이 사용 가능)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//리눅스헤더
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>

#define BUFSIZE 1024
#define MAX_CLIENT 64

int clnt_socks[MAX_CLIENT];
int clnt_id = 0;//접속 순서대로
char nicknames[MAX_CLIENT][50];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 모든 접속자에게 안전하게 메시지를 전송한다. except_sock은 제외할 소켓이며 -1이면 모두에게 보낸다. */
void broadcast_message(const char *message, int except_sock){
    int i;
    pthread_mutex_lock(&clients_mutex);
    for(i = 0; i < clnt_id; i++){
        if(clnt_socks[i] != except_sock)
            write(clnt_socks[i], message, strlen(message));
    }
    pthread_mutex_unlock(&clients_mutex);
}

void error_handling(const char* message){ 
    fputs(message, stderr);
    fputc('\n',stderr);
    exit(-1);
}
/*입장·퇴장 알림에 표시할 현재 날짜와 시간을 만든다. */
void get_current_time(char *time_str){
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    int hour = t->tm_hour;
    const char *ampm;

    if(hour < 12)
        ampm = "오전";
    else
        ampm = "오후";

    if(hour == 0)
        hour = 12;
    else if(hour > 12)
        hour -= 12;

    sprintf(time_str, "%02d.%02d.%02d. %s %d시 %02d분",
        t->tm_year % 100, t->tm_mon + 1, t->tm_mday,
        ampm, hour, t->tm_min);
}

void* handle_clnt(void* arg){
    int clnt_sock = *((int*)arg);
    free(arg);
    int str_len = 0;
    char msg[BUFSIZE] = {0,};
    int idx;
    int my_idx = -1;
    char my_nickname[50] = "이름 없음";

    for(idx =0;idx<clnt_id;idx++){
        if(clnt_sock == clnt_socks[idx]){
            my_idx = idx;
            break;
        }
    }

    while(0 < (str_len = read(clnt_sock, msg, sizeof(msg)-1))){ 
        msg[str_len] = '\0';
        /* /nick으로 전달된 닉네임을 해당 클라이언트 정보에 등록한다. */
        if(strncmp(msg, "/nick ", 6) == 0){
            strncpy(my_nickname, msg + 6, sizeof(my_nickname) - 1);
            my_nickname[sizeof(my_nickname) - 1] = '\0';
            my_nickname[strcspn(my_nickname,"\n")] ='\0';

            pthread_mutex_lock(&clients_mutex);
            for(idx = 0; idx < clnt_id; idx++){
                if(clnt_sock == clnt_socks[idx]){
                    strncpy(nicknames[idx], my_nickname, sizeof(nicknames[idx]) - 1);
                    nicknames[idx][sizeof(nicknames[idx]) - 1] = '\0';
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            /* 닉네임과 접속시간을 포함한 입장 알림을 전체 전송한다. */
            char time_str[50];
            char enter_msg[BUFSIZE];

            get_current_time(time_str);
            snprintf(enter_msg, sizeof(enter_msg),
                    "[입장] %s님을 환영합니다  |  접속시간: %s\n",
                    my_nickname, time_str);
            printf("[ENTER] %s (%s)\n", my_nickname, time_str);
            fflush(stdout);

            broadcast_message(enter_msg, -1);
            continue;
        }

        /* /notice 내용을 공지 전용 형식으로 만들어 전체 전송한다. */

        if(strcmp(msg, "/notice\n")==0){
            printf("공지 내용을 입력해야 합니다.\n");
            printf("사용 방법: /notice 공지내용\n");
            continue;
        }

        if(strncmp(msg, "/notice ", 8) == 0){
            char notice_msg[BUFSIZE+200];
            char *notice_content = msg+8;

            printf("[NOTICE] %s : %s", my_nickname, notice_content);
            fflush(stdout);

            snprintf(notice_msg,
                    sizeof(notice_msg),
                    "\n"
                    "==================================================\n"
                    "                       공지\n"
                    "--------------------------------------------------\n"
                    "작성자 : %s\n"
                    "\n"
                    "%s"
                    "==================================================\n\n",
                    my_nickname,
                    notice_content);
            broadcast_message(notice_msg, -1);

            memset(msg, 0, sizeof(msg));
            continue;
        }

        //일반 채팅
        broadcast_message(msg, clnt_sock);
        memset(msg, 0, sizeof(msg));
        
    }

    
    my_idx = -1;

    for(idx = 0; idx < clnt_id; idx++){
        if(clnt_sock == clnt_socks[idx]){
            my_idx = idx;
            break;
        }
    }

    /* 연결이 끊긴 사용자의 닉네임과 퇴장시간을 전체에 알린다. */
    if(my_idx != -1){
        char leave_msg[BUFSIZE];
        char time_str[100];
        get_current_time(time_str);

        snprintf(leave_msg,
                sizeof(leave_msg),
                "[퇴장] %s님이 퇴장했습니다  |  접속시간: %s\n",
                my_nickname, time_str);
        printf("[LEAVE] %s (%s)\n",my_nickname,time_str);
        fflush(stdout);
        
        broadcast_message(leave_msg, clnt_sock);
    }

    pthread_mutex_lock(&clients_mutex);
    for(idx=0;idx<clnt_id; idx++){
        if(clnt_sock == clnt_socks[idx]){
            int j;
            for(j=idx;j<clnt_id-1;j++){
                clnt_socks[j] = clnt_socks[j+1];
                strcpy(nicknames[j],nicknames[j+1]);
            }
            clnt_id--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(clnt_sock); 
    return NULL;

}

int main(int argc, char* argv[]){
    
    int serv_sock;
    int clnt_sock;
    pthread_t t_id;
    
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    unsigned int clnt_addr_size; 

    if(2 != argc){
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }
   
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if(-1 == serv_sock){
        error_handling("socket() error");
    }


    memset(&serv_addr, 0, sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    
    if(-1 == bind(serv_sock,(struct sockaddr*)&serv_addr, sizeof(serv_addr))){
        error_handling("bind() error");
    }

    if(-1 == listen(serv_sock, 5)){
        error_handling("listen() error");
    }

    clnt_addr_size = sizeof(clnt_addr);

    while(1){
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        
        if(-1 == clnt_sock){
            error_handling("accept() error");
        }

        pthread_mutex_lock(&clients_mutex);
        if(clnt_id >= MAX_CLIENT){
            pthread_mutex_unlock(&clients_mutex);
            write(clnt_sock, "서버의 최대 접속 인원을 초과했습니다.\n", strlen("서버의 최대 접속 인원을 초과했습니다.\n"));
            close(clnt_sock);
            continue;
        }
        clnt_socks[clnt_id++] = clnt_sock;
        pthread_mutex_unlock(&clients_mutex);

        int *sock_arg = (int *)malloc(sizeof(int));

        if(sock_arg == NULL){
            close(clnt_sock);
            error_handling("malloc() error");
        }
        *sock_arg = clnt_sock;

        pthread_create(&t_id, NULL, handle_clnt, (void*)sock_arg);
        pthread_detach(t_id);

    }

    return 0;
}