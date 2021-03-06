
#include "storage.h"
#include "conn.h"
#include "threadpool.h"
#include "worker.h"
#include "util.h"
#include "protocol.h"

#define PENDING_SIZE 50

response_t **clients;

//ritorno l'inidce massimo dei descrittori attivi
int updatemax(fd_set set, int fdmax){
    for (int i = fdmax-1; i >= 0 ; --i) {
        if(FD_ISSET(i, &set))
            return i;
    }
    return -1;
}


int main(int argc, char *argv[]){

    CHECK_EQ_EXIT(atexit(cleanup), -1, "atexit")
    if(argc < 3){
        fprintf(stderr, "Usage: %s -f <configfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //lettura configfile
    configArgs cargs = {"", 0, 0, 0};
    int opt;
    while ((opt = getopt(argc, argv, ":f:")) != -1){
        switch (opt) {
            case 'f':
                CHECK_ERROR_EXIT(parse_config(optarg, &cargs), -1)
                break;
            case ':':
                PRINT_ERROR("-f option requires an argument")
                exit(EXIT_FAILURE);
            default:
                PRINT_ERROR("Unrecognised option");
                exit(EXIT_FAILURE);
        }
    }

    //creazione threadpool
    threadpool_t *tpool;
    CHECK_EQ_EXIT(tpool = createThreadPool((int)cargs.nworkers, PENDING_SIZE),NULL,
                  "create threadpool")

    //creazione socket
    unlink(cargs.sktname);
    __attribute__((unused)) int unused;
    int listenfd;
    SYSCALL_EXIT(socket, listenfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "")
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, cargs.sktname, strlen(cargs.sktname));
    SYSCALL_EXIT(bind, unused, bind(listenfd, (struct sockaddr*)&sa, sizeof(sa)), "bind", "")
    SYSCALL_EXIT(listen, unused, listen(listenfd, MAXBACKLOG), "listen", "")

    MALLOC(clients, MAXBACKLOG, response_t*)
    for(int i = 0; i < MAXBACKLOG; i++)
        MALLOC(clients[i], 1, response_t)
    int fd_max, fd;
    fd_set set, rset, wset;
    FD_ZERO(&set);

    //all'inizio l'unico socket nell'insieme sarà quello di welcome
    fd_max = listenfd;
    FD_SET(listenfd, &set);
    while(true) {
        rset = set;

        if (select(fd_max + 1, &rset, &wset, NULL, NULL) == -1) {
            perror("select");
            return EXIT_FAILURE;
        }

        //iteriamo nel range [0, fd_max] nel quale troveremo
        //tutti i fd che sono pronti per una operazione di i/o
        for (fd = 0; fd <= fd_max; fd++) {

            int ret;
            //client fd pronto per un operazione di connessione/lettura
            if (FD_ISSET(fd, &rset)) {
                //listen socket pronto per accettare una nuova connessione
                if (fd == listenfd) {
                    //nuova richiesta di connessione
                    int fd_client;
                    SYSCALL_EXIT(accept, fd_client, accept(listenfd, NULL, NULL), "accept", "")
                    printf("Nuova connessione: fd = %d\n", fd_client);
                    FD_SET(fd_client, &set); //aggiungo il fd al set generale
                    if (fd_client > fd_max) fd_max = fd_client;
                } else {
                    CHECK_EQ_EXIT((ret = addToThreadPool(tpool, (void*)requesthandler, (void*)&fd)), -1, "add threadpool")
                    if(ret == 1) { //in questo caso il threadpool ha ritornato coda piena
                        if (tpool->exiting) //se è > 0 il threadpool non accetta nuovi task
                            FD_CLR(fd, &rset);
                        continue;
                    }
                    //adesso che abbiamo letto la richiesta del client lo inseriamo nell'insieme dei fd pronti per
                    //un'operazione di scrittura
                    FD_SET(fd, &wset);
                }
            }

                //client fd pronto per un'operazione di scrittura
            else if (FD_ISSET(fd, &wset)) {
                CHECK_EQ_EXIT(ret = addToThreadPool(tpool, (void*) requesthandler, (void*)&fd), -1, "add threadpool")
                if(ret == 1) {
                    if (tpool->exiting)
                        FD_CLR(fd, &wset);
                    continue;
                }
                FD_SET(fd, &rset);
            }
        }
    }
}




}

void cleanup(){
    printf("cleanup\n");
}