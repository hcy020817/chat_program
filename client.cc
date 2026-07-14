#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

/*
 * [확장 기능 설계 - 클라이언트]
 * 1. 닉네임: 사용자가 입력한 닉네임을 모든 일반 메시지 앞에 표시한다.
 * 2. 메시지 기록/검색: 최근 메시지를 최대 100개 저장하고 키워드로 검색한다.
 * 3. 답글: 기록의 메시지 번호를 선택해 원문을 인용한 답글을 작성한다.
 * 4. 공지: /notice 명령으로 일반 채팅과 구분되는 공지 메시지를 요청한다.
 * 5. 터미널 UI: ANSI 색상, 입력 프롬프트, 도움말, 화면 정리 기능을 제공한다.
 * 6. 날짜 표시: 채팅 날짜가 바뀔 때 날짜 구분선을 한 번만 출력한다.
 */

#define BUFSIZE 1024
#define MAX_HISTORY 100
#define HISTORY_SIZE 1200

int sock;

char message_history[MAX_HISTORY][HISTORY_SIZE];
int history_count =0;

int last_year = -1;
int last_month = -1;
int last_day = -1;

/* 수신 스레드와 입력 스레드가 기록 및 화면을 동시에 사용하는 상황을 보호한다. */
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;
int running = 1;

/* 시스템 알림, 공지, 기능 제목, 입력 프롬프트를 색으로 구분한다. */
#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[1;32m"
#define COLOR_CYAN   "\033[1;36m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_GRAY   "\033[0;90m"


//화면 출력 관련 함수
void print_prompt(void)
{
    printf(COLOR_GREEN "> " COLOR_RESET);
    fflush(stdout);
}

void print_line(void)
{
    printf(COLOR_GRAY "==================================================" COLOR_RESET "\n");
}

void print_subline(void)
{
    printf(COLOR_GRAY "--------------------------------------------------" COLOR_RESET "\n");
}

/*  검색과 답글에 사용할 최근 메시지를 최대 100개까지 저장한다. */
void save_message(const char *message){
    pthread_mutex_lock(&history_mutex);

    if(history_count <MAX_HISTORY){
        strncpy(message_history[history_count], message, HISTORY_SIZE-1);
        message_history[history_count][HISTORY_SIZE-1]='\0';
        history_count++;
    }
    else{
        //저장공간 가득 차면 가장 오래된 메시지 삭제 후 나머지 메시지 한 칸씩 앞으로 이동
        int i;
        for(i =0;i<MAX_HISTORY-1;i++){
            strcpy(message_history[i],message_history[i+1]);
        }
        strncpy(message_history[MAX_HISTORY-1], message, HISTORY_SIZE-1);
        message_history[MAX_HISTORY-1][HISTORY_SIZE-1] = '\0';
    }

    pthread_mutex_unlock(&history_mutex);
}

/* 저장된 모든 메시지에서 입력한 키워드가 포함된 메시지를 찾는다. */
void search_messages(const char *keyword){
    int i;
    int found_count =0;
    
    pthread_mutex_lock(&history_mutex);
    printf("\n");
    print_line();
    printf(COLOR_CYAN "                 메시지 검색 결과" COLOR_RESET "\n");
    print_subline();

    printf("검색어 : %s\n\n", keyword);

    for(i = 0; i < history_count; i++){
        if(strstr(message_history[i], keyword) != NULL){
            printf("%d. %s", found_count + 1, message_history[i]);

            int len = strlen(message_history[i]);

            if(len == 0 ||
               message_history[i][len - 1] != '\n'){
                printf("\n");
            }

            found_count++;
        }
    }

    if(found_count == 0){
        printf("검색 결과가 없습니다.\n");
    }

    printf("\n");
    print_line();

    if(found_count>0){
        printf("총 %d개의 메시지를 찾았습니다.\n", found_count);
        print_line();
    }
    printf("\n");
    fflush(stdout);

    pthread_mutex_unlock(&history_mutex);
}

void show_message_history(void)
{
    /* 답글에 사용할 번호와 함께 최근 메시지 기록을 출력한다. */
    int i;

    pthread_mutex_lock(&history_mutex);

    printf("\n");
    print_line();
    printf(COLOR_CYAN "                   메시지 기록" COLOR_RESET "\n");
    print_subline();
    printf("\n");

    if(history_count == 0){
        printf("저장된 메시지가 없습니다.\n");
    }
    else{
        for(i = 0; i < history_count; i++){
            printf("%d. %s", i + 1, message_history[i]);

            // 메시지 끝에 줄바꿈이 없으면 직접 추가한다.
            int len = strlen(message_history[i]);

            if(len == 0 || message_history[i][len - 1] != '\n'){
                printf("\n");
            }
        }
    }

    printf("\n");
    print_line();
    printf("\n");

    fflush(stdout);

    pthread_mutex_unlock(&history_mutex);
}

int make_reply_message(int message_number,const char *reply_content, const char *nickname,char *reply_msg,int reply_msg_size)
{
     /* 선택한 원문과 작성자의 닉네임을 포함하는 답글 형식을 만든다. */
    int index = message_number - 1;
    char original_message[HISTORY_SIZE];

    pthread_mutex_lock(&history_mutex);

    //사용자가 입력한 번호가 현재 기록 범위를 벗어났는지 검사한다.
     
    if(index < 0 || index >= history_count){
        pthread_mutex_unlock(&history_mutex);
        return 0;
    }

    strncpy(original_message,
            message_history[index],
            sizeof(original_message) - 1);

    original_message[sizeof(original_message) - 1] = '\0';

    pthread_mutex_unlock(&history_mutex);

    // 기존 메시지 끝의 줄바꿈 제거
    
    original_message[strcspn(original_message, "\n")] = '\0';

    //마지막 답글까지 자연스럽게 줄바꿈된다.
     
    snprintf(reply_msg,reply_msg_size,"\" %s \"에 답장\n-->[%s]%s",original_message,nickname,reply_content);

    return 1;
}

/* 사용자가 명령어를 외우지 않아도 되도록 사용법을 제공한다. */
void print_command_guide(void)
{
    printf("\n");
    print_line();
    printf(COLOR_CYAN "                 사용 가능한 명령어" COLOR_RESET "\n");
    print_subline();

    printf("/quit                  채팅 종료\n");
    printf("/notice 공지내용       공지 작성\n");
    printf("/search 검색어         메시지 검색\n");
    printf("/history               메시지 기록 확인\n");
    printf("/reply 번호 답글내용   특정 메시지에 답글\n");
    printf("/help                  명령어 도움말\n");
    printf("/clear                 터미널 화면 정리\n");

    print_line();
    printf("\n");
}

void* recv_msg(void* arg){
    (void)arg;
    char msg[BUFSIZE];

    while(true){
        memset(msg, 0, sizeof(msg));
        int len = read(sock, msg, sizeof(msg) - 1);

        if(len<=0){
            running = 0;
            pthread_mutex_lock(&screen_mutex);
            printf("\r\033[2K\n" COLOR_YELLOW "서버와 연결이 종료되었습니다." COLOR_RESET "\n");
            fflush(stdout);
            pthread_mutex_unlock(&screen_mutex);
            break;
        }
        //문자열 끝 표시
        msg[len] = '\0';
        save_message(msg);

        /* 수신 메시지가 사용자가 입력하는 줄에 이어 붙지 않도록 현재 줄을 지운다. */
        pthread_mutex_lock(&screen_mutex);
        printf("\r\033[2K");

        /*시스템 알림·공지를 일반 채팅과 다른 색으로 표시한다. */
        if(strncmp(msg, "[입장]", strlen("[입장]")) == 0 ||
           strncmp(msg, "[퇴장]", strlen("[퇴장]")) == 0){
            printf(COLOR_GRAY "%s" COLOR_RESET, msg);
        }
        else if(strstr(msg, "                       공지") != NULL){
            printf(COLOR_YELLOW "%s" COLOR_RESET, msg);
        }
        else{
            printf("%s", msg);
        }
        if(msg[0] != '\0' && msg[strlen(msg) - 1] != '\n') printf("\n");
        print_prompt();
        pthread_mutex_unlock(&screen_mutex);
    }
    return NULL;

}

//채팅방 시작할때 날짜 표시
void print_current_date(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    printf("\n----------%04d년 %d월 %d일----------\n\n",
           t->tm_year + 1900,
           t->tm_mon + 1,
           t->tm_mday);

    fflush(stdout);
}

void print_date_if_changed(void)
{
    /* 날짜가 달라진 경우에만 회색 날짜 구분선을 출력한다. */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    int year = t->tm_year + 1900;
    int month = t->tm_mon + 1;
    int day = t->tm_mday;

    if(year != last_year ||
       month != last_month ||
       day != last_day){

        printf("\n");
        printf(COLOR_GRAY "----------------%04d년 %02d월 %02d일----------------" COLOR_RESET "\n",
               year, month, day);
        printf("\n");

        last_year = year;
        last_month = month;
        last_day = day;

        fflush(stdout);
    }
}

int main(int argc, char *argv[]){
    struct sockaddr_in serv_addr;
    pthread_t recv_thread;
    char nickname[50];

    if(argc!=3){
        printf("Usage: %s <IP> <port>\n",argv[0]);
        return 1;
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);

    if(sock == -1){
        printf("socket() error\n");
        return 1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1){
        printf("connect() error\n");
        close(sock);
        return 1;
    }

    printf("\n" COLOR_CYAN "채팅 프로그램" COLOR_RESET
           "  |  서버에 연결되었습니다\n");

    printf("\n");
    print_line();
    printf(COLOR_CYAN "                   닉네임 설정" COLOR_RESET "\n");
    print_line();
    
     /*닉네임: 빈 닉네임과 지나치게 긴 닉네임을 허용하지 않는다. */
    do {
        printf("닉네임을 입력하세요 (1~20자): ");
        if(fgets(nickname, sizeof(nickname), stdin) == NULL){
            close(sock);
            return 0;
        }
        nickname[strcspn(nickname,"\n")] = '\0';
        if(strlen(nickname) == 0 || strlen(nickname) > 20)
            printf(COLOR_YELLOW "닉네임은 1~20자로 입력해주세요.\n" COLOR_RESET);
    } while(strlen(nickname) == 0 || strlen(nickname) > 20);

    print_command_guide();

    printf("채팅을 시작합니다.\n");

    print_date_if_changed();
    
    pthread_create(&recv_thread, NULL, recv_msg, NULL);
    pthread_detach(recv_thread);

     /* 서버가 입장 알림에 사용할 닉네임을 /nick 명령으로 등록한다. */
    char nick_msg[100];
    sprintf(nick_msg, "/nick %s\n", nickname);
    write(sock, nick_msg, strlen(nick_msg));

    print_prompt();
    while(running){
        char input[BUFSIZE];
        char send_msg[BUFSIZE+100];

        if(fgets(input, sizeof(input), stdin)==NULL){
            break;
        }

        if(strcmp(input, "/quit\n")==0){
            break;
        }
        if(strcmp(input, "\n") == 0){
            print_prompt();
            continue;
        }
        /* 채팅 중 명령어 안내를 다시 확인한다. */
        if(strcmp(input, "/help\n") == 0){
            pthread_mutex_lock(&screen_mutex);
            print_command_guide();
            print_prompt();
            pthread_mutex_unlock(&screen_mutex);
            continue;
        }
        if(strcmp(input, "/clear\n") == 0){
            /* 채팅 상단만 다시 표시한다. */
            pthread_mutex_lock(&screen_mutex);
            printf("\033[2J\033[H");
            printf(COLOR_CYAN "채팅 프로그램" COLOR_RESET "  |  서버에 연결되었습니다\n");
            print_prompt();
            pthread_mutex_unlock(&screen_mutex);
            continue;
        }
        if(strcmp(input, "/history\n")==0){
            show_message_history();
            print_prompt();
            continue;
        }
        //검색어 없이 /search만 입력한 경우
        if(strcmp(input,"/search\n") ==0){
            printf("검색어를 입력해야 합니다.\n");
            printf("사용 방법: /search 검색어\n");
            continue;
        }

        /* /search 뒤의 문자열을 키워드로 사용해 메시지를 검색한다. */
        if(strncmp(input, "/search ", 8)==0){
            char *keyword = input+8;
            keyword[strcspn(keyword,"\n")] = '\0';

            if(strlen(keyword)==0){
                printf("검색어를 입력해야 합니다.\n");
                continue;
            }

            search_messages(keyword);
            print_prompt();
            continue;
        }

        /* 답글 명령어만 입력한 경우 */
        if(strcmp(input, "/reply\n") == 0){
            printf("사용 방법: /reply 메시지번호 답글내용\n");
            printf("메시지 번호 확인: /history\n");
            continue;
        }
        
        /* /reply 메시지번호 답글내용 형식으로 답글을 작성한다. */
        if(strncmp(input, "/reply ", 7) == 0){
            int message_number;
            int consumed = 0;
            char *reply_content;
            char reply_msg[BUFSIZE * 2];

            /*
            * %n은 메시지 번호를 읽은 다음 위치를 consumed에 저장한다.
            * 예: "/reply 3 안녕하세요"
            *               ↑ consumed가 이 위치를 나타냄
            */
            if(sscanf(input + 7, "%d %n",
                    &message_number,
                    &consumed) != 1){
                printf("메시지 번호를 올바르게 입력해야 합니다.\n");
                printf("사용 방법: /reply 메시지번호 답글내용\n");
                continue;
            }

            reply_content = input + 7 + consumed;

            if(strlen(reply_content) == 0 ||
            strcmp(reply_content, "\n") == 0){
                printf("답글 내용을 입력해야 합니다.\n");
                printf("사용 방법: /reply 메시지번호 답글내용\n");
                continue;
            }

            if(!make_reply_message(message_number,
                                reply_content,
                                nickname,
                                reply_msg,
                                sizeof(reply_msg))){
                printf("존재하지 않는 메시지 번호입니다.\n");
                printf("메시지 번호를 확인하려면 /history를 입력하세요.\n");
                continue;
            }

            /*
            * 본인이 작성한 답글도 검색 및 기록에 포함한다.
            * 현재 서버가 발신자에게 메시지를 돌려보내지 않는 구조일 때 필요하다.
            */
            save_message(reply_msg);

            write(sock, reply_msg, strlen(reply_msg));
            print_prompt();
            continue;
        }
        
        //공지 내용 누락
        if(strcmp(input, "/notice\n")==0){
            printf("공지 내용을 입력해야 합니다.\n");
            printf("사용 방법: /notice 공지내용\n");
            continue;
        }

         /* 공지 작성 요청은 서버가 모든 접속자에게 전송하도록 전달한다. */
        if(strncmp(input, "/notice ", 8) == 0){
            write(sock, input, strlen(input));
            print_prompt();
            continue;
        }

        //일반 메시지에는 클라이언트에서 닉네임을 붙인다.
        sprintf(send_msg,"[%s] %s", nickname, input);

        save_message(send_msg);

        write(sock, send_msg, strlen(send_msg));
        print_prompt();
    }

    close(sock);

    printf("클라이언트를 종료합니다.\n");

    return 0;
}