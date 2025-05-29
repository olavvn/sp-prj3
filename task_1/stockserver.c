/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

/* Start: Define Structs*/
typedef struct{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex;
}item;

typedef struct{
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
}pool;

typedef struct node{
    item item;
    struct node *left;
    struct node *right;
}node;

node*root;
/* End: Define Structs*/

/* Start: Define Functions*/
void echo(int connfd);
void sigint_handler(int sig);
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
void load_stock();
void save_stock(node* node, char* output_buf);
void show_stock(node* node, char* output_buf);
void sell_stock(int stockid, int amount);
int buy_stock(int stockid, int amount);
/* End: Define Functions*/

/*Start: Main Function*/
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    static pool pool;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    Signal(SIGINT, sigint_handler);

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    /*Load Stock Data*/
    load_stock();

    listenfd = Open_listenfd(argv[1]);

    /*Initialize Pool*/
    init_pool(listenfd, &pool);

    while (1) {
        /* Wait for listening/connectind descriptor(s) to become ready*/
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
        if (FD_ISSET(listenfd, &pool.ready_set)){
	        clientlen = sizeof(struct sockaddr_storage); 
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            /*Add client*/
            add_client(connfd, &pool);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, 
            MAXLINE, client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);

        }
        /*Echo a text line from each ready connected descriptor*/
        check_clients(&pool);
    }
    exit(0);
}
/*End: Main Function*/

/*Start: sigint_handler*/
void sigint_handler(int sig) {
    printf("Terminate server\n");
    int r;
    r = remove("stock.txt"); 
    if (r != -1) {
        FILE *fp;
        fp = fopen("stock.txt", "w");
        if (fp != NULL) { 
            strsave(root, fp);
            fclose(fp);
        } else {
            printf("Error: Failed to open stock.txt for writing.\n");
        }
    } else {
        printf("Error : fail to remove stock.txt\n");
    }
    freenode(root); 
    exit(1);        
}
/*End: sigint_handler*/

/*Start: init_pool*/
void init_pool(int listenfd, pool*p){
    int i;
    p -> maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++) p -> clientfd[i] = -1;
    p -> maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p -> read_set);
}
/*End: init_pool*/

/*Start: add_client*/
void add_client(int connfd, pool *p){
    int i;
    p -> nready--;
    for (i = 0; i < FD_SETSIZE; i++) /*Find an available slot*/
        if (p -> clientfd[i] < 0){
            p -> clientfd[i] = connfd;
            Rio_readinitb(&p -> clientrio[i], connfd);
            FD_SET(connfd, &p -> read_set);
            if (connfd > p->maxfd) 
	    	    p -> maxfd = connfd;
	        if (i > p -> maxi)      
	    	    p -> maxi = i;      
	        break;
        }
    if (i == FD_SETSIZE)
        app_error("add_client error: Too many clients");
}   
/*End: add_client*/

/*Start: check_clients*/
void check_clients(pool *p) 
{
    int i, connfd, n;
    char buf[MAXLINE]; 
    char output_buf[MAXLINE];
    rio_t rio;
    int command_was_processed;

    for (i = 0; (i <= p -> maxi) && (p -> nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        if ((connfd > 0) && (FD_ISSET(connfd, &p -> ready_set))) { 
            p -> nready--;
            n = Rio_readlineb(&rio, buf, MAXLINE);
            if (n != 0) {
                printf("Server received %d bytes on fd %d\n", n, connfd);
                Rio_writen(connfd, buf, n);
                output_buf[0] = '\0';
                command_was_processed = 0;

                /*Prompt: Show*/
                if (command_was_processed == 0 && (buf, "show", 4) == 0){
                    command_was_processed = 1;
                    show_stock(root, output_buf);
                    Rio_writen(connfd, output_buf, MAXLINE);
                }

                /*Prompt: Buy*/
                if (command_was_processed == 0 && strncmp(buf, "buy ", 4) == 0){
                    command_was_processed = 1;
                    int stockid, amount;
                    if (sscanf(buf + 4, "%d %d", &stockid, &amount) == 2){
                        if (buy_stock(stockid, amount))
                            strcpy(output_buf, "[buy] success\n");
                        else
                            strcpy(output_buf, "Not enough left stock\n");
                        Rio_writen(connfd, output_buf, MAXLINE);
                    }
                }

                /*Prompt: Sell*/
                if (command_was_processed == 0 && strncmp(buf, "sell ", 5) == 0){
                    command_was_processed = 1;
                    int stockid, amount;
                    if (sscanf(buf + 5, "%d %d", &stockid, &amount) == 2){
                        strcpy(output_buf, "[sell] success\n");
                        sell_stock(stockid, amount);
                        Rio_writen(connfd, output_buf, MAXLINE);
                    }
                }
            }

            /* EOF detected, remove descriptor from pool */
            else { 
                output_buf[0] = '\0';
                Close(connfd);
                FD_CLR(connfd, &p->read_set); 
                p->clientfd[i] = -1;          
            }
	}
    }
}
/*End: check_clients*/

/*Start: load_stock*/
void load_stock(){

}
/*End: load_stock*/

/*Start: save_stock*/
void save_stock(node* nd, char* output_buf){
    FILE* fp = Fopen("stock.txt", "w");
    show_stock(nd, output_buf);
    fputs(output_buf, fp);
    Fclose(fp);
}
/*End: save_stock*/

/*Start: show_stock*/
void show_stock(node* nd, char* output_buffer){
    node* current_node = nd;
    char str[MAXLINE];
    if(current_node !=NULL){
        sprintf(str,"%d %d %d\n",
            current_node->item.ID,
            current_node->item.left_stock,
            current_node->item.price);
    }
    strcat(output_buffer,str);
    if(current_node->left !=NULL){
        show_stock(current_node->left,output_buffer);
    }
    if(current_node->right !=NULL){
        show_stock(current_node->right,output_buffer);
    }
}
/*Start: show_stock*/

/*Start: sell_stock*/
void sell_stock(int stockid, int amount);
/*End: sell_stock*/

/*Start: buy_stock*/
int buy_stock(int stockid, int amount);
/*Start: buy_stock*/



/* $end echoserverimain */
