
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>


#define FILENAME_MAXSIZE 255
#define TERMINATOR 1
#define MODE_MAXSIZE 255
#define DATA_MAXSIZE 512
#define HEADER_SIZE 4
#define PACKET_MAXSIZE (DATA_MAXSIZE+HEADER_SIZE)
#define DATA_MINSIZE 4


////////////////////////$  Structs  $///////////////////////////////

struct ACK_ {
    short int opcode;
    unsigned short int block_num;
}__attribute__((packed));

struct WRQ_ {
    short int opcode;
    char wrq_data[FILENAME_MAXSIZE + TERMINATOR + MODE_MAXSIZE + TERMINATOR];
}__attribute__((packed));


struct DATA_ {
    short int opcode;
    unsigned short int block_num;
    char data[DATA_MAXSIZE];
}__attribute__((packed));


typedef struct DATA_ DATA;
typedef struct WRQ_ WRQ;
typedef struct ACK_ ACK;

////////////////////////$  added functions  $///////////////////////////////
void clearSocket(int socketNum);
void fatalError(int socketNum, FILE *dataFile);
int serverSession(int argc, char* argv[]);


////////////////////////$  Main program  $///////////////////////////////
//********************************************
// function name: main
// Description: manages the resources as well as receiving and sending packets to and from the client
// Parameters: argc, argv- the number of arguments and the arguments themselves
// Returns: int as defined in code
//**************************************************************************************
int main(int argc, char* argv[]) {

    if(argc != 2 ){ //incorrect number of arguments
        printf("Error - wrong number of arguments %d\n", argc);
        return 1;
    }

    if (!atoi(argv[1])) { //if not valid number
        printf("Error - the argument isn't a number\n");
        return 1;
    }
    while (true) {
        int succes = serverSession(argc, argv);
        if (succes == 1) {
            printf("RECVFAIL\n");
        } else {
            printf("RECVOK\n");
        }
    }
    return 0;
}


////////////////////////$  functions  $///////////////////////////////
int serverSession(int argc, char* argv[]) {
    const int WAIT_FOR_PACKET_TIMEOUT = 3;
    const int NUMBER_OF_FAILURES = 7;
    int sockNum;
    struct sockaddr_in servAddr;
    struct sockaddr_in clntAddr;
    unsigned int cliAddrLen;
    unsigned short servPort = atoi(argv[1]);
    int recvMsgSize;
    fd_set file_des;



    // create socket
    if ((sockNum = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("TTFTP_ERROR: ");
        return 1;
    }

    //construct local address structure
    //zero out structure
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;

    //local port
    servAddr.sin_port = htons(servPort);

    //bind to the local address
    if (bind(sockNum, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
        perror("TTFTP_ERROR: ");
        return 1;
    }

    struct timeval time_out;
    ACK ack_block;
    WRQ wrq_block;
    DATA data_block;
    char file_name[FILENAME_MAXSIZE+TERMINATOR];
    char mode[MODE_MAXSIZE+TERMINATOR];
    int select_res = 0;
    int ack_block_cntr = 0;

    //set the size of the inout parameter


    // Receive message from client expecting WRQ block
    cliAddrLen = sizeof(clntAddr);
    if ((recvMsgSize = recvfrom(sockNum, &wrq_block, PACKET_MAXSIZE, 0,
                                (struct sockaddr *) &clntAddr, &cliAddrLen)) < 0) {
        perror("TTFTP_ERROR: ");
        clearSocket(sockNum);
        close(sockNum);
        return 1;
    }

    if ( recvMsgSize < HEADER_SIZE + DATA_MINSIZE - 2 ){ //if the data from the wrq is too small
        printf("Error - insufficient data in WRQ packet\n");
        clearSocket(sockNum);
        close(sockNum);
        return 1;
    }

    //parsing and checking the data from the client
    if (ntohs(wrq_block.opcode) != 2){
        // it is not a wrq block - error
        printf("Error - wrong opcode for wrq\n");
        clearSocket(sockNum);
        close(sockNum);
        return 1;
    }

    //getting file name
    strcpy(file_name, wrq_block.wrq_data);
    int file_name_length =strlen(file_name)+1;
    strcpy(mode, wrq_block.wrq_data + file_name_length);

    printf("IN:WRQ, %s, %s\n",file_name,mode);

    //check if it's in the right mode
    if(strcmp(mode,"octet") !=0 ){
        printf("Error - transition mode isn't octet\n");
        clearSocket(sockNum);
        close(sockNum);
        return 1;
    }

    //open file once WRQ is received
    FILE *DataFile = fopen(file_name, "w");
    if(DataFile==NULL){
        perror("TTFTP_ERROR:");
        //clear socket and close
        clearSocket(sockNum);
        close(sockNum);
        return 1;
    }

    //create ack packet
    ack_block.opcode= htons(4);
    ack_block.block_num=htons(ack_block_cntr);
    ack_block_cntr++;
    if(sendto(sockNum, &ack_block, sizeof(ack_block), 0, (struct sockaddr *)&clntAddr, sizeof(clntAddr)) != sizeof(ack_block)){ // check send all of packet correctly
        perror("TTFTP_ERROR:");
        fatalError(sockNum, DataFile);
        return 1;
    }
    else {//sending packet was successful, print that ACK packet was sent to client
        printf("OUT:ACK, %d\n", ack_block_cntr-1);
    }
    int timeoutExpiredCount = 0;
    int last_write_size = 0;
    do {
        do {

            do {
                // init the file descriptor
                FD_ZERO(&file_des);
                FD_CLR(sockNum, &file_des);
                FD_SET(sockNum, &file_des);
                // Wait WAIT_FOR_PACKET_TIMEOUT to see if something appears
                // for us at the socket (we are waiting for DATA)
                time_out.tv_sec = WAIT_FOR_PACKET_TIMEOUT;
                time_out.tv_usec = 0;
                if((select_res = select(sockNum + 1, &file_des, NULL, NULL, &time_out )) < 0){
                    perror("TTFTP_ERROR: ");
                    fatalError(sockNum, DataFile);
                    return 1;
                }

                if (select_res > 0) {// if there was something at the socket and
                    // we are here not because of a timeout
                    //receiving Data packet
                    cliAddrLen = sizeof(clntAddr);
                    if((recvMsgSize = recvfrom(sockNum, &data_block, PACKET_MAXSIZE, 0, (struct sockaddr *) &clntAddr, &cliAddrLen)) < 0) {
                        //if recvfrom had systemcall error
                        perror("TTFTP_ERROR: ");
                        clearSocket(sockNum);
                    }
                    else { // recieved data ok. check size of packet print received packet write to file and print writing to file
                        data_block.opcode = ntohs(data_block.opcode);
                        data_block.block_num = ntohs(data_block.block_num);


                        //pring data msg
                        printf("IN:DATA, %d, %d\n", data_block.block_num, recvMsgSize -HEADER_SIZE);

                    }
                }
                if (select_res == 0) // Time out expired while waiting for data
                    // to appear at the socket
                {
                    ack_block.opcode= htons(4);
                    ack_block.block_num=htons(ack_block_cntr-1);
                    if(sendto(sockNum, &ack_block, sizeof(ack_block), 0, (struct sockaddr *)&clntAddr, sizeof(clntAddr)) != sizeof(ack_block)){ // check send all of packet correctly
                        perror("TTFTP_ERROR:");
                        fatalError(sockNum, DataFile);
                        return 1;
                    }
                    else {//sending packet was successful, print that ACK packet was sent to client
                        printf("OUT:ACK, %d\n", ack_block_cntr-1);
                    }
                    timeoutExpiredCount++;
                }

                if (timeoutExpiredCount >= NUMBER_OF_FAILURES) {
                    // FATAL ERROR BAIL OUT
                    fatalError(sockNum, DataFile);
                    return 1;
                }

            } while ((select_res > 0  && recvMsgSize < 0) || (select_res == 0) ); // Continue while some socket was ready
            // but recvfrom somehow failed to read the data

            if (data_block.opcode != 3) // We got something else but DATA
            {
                // FATAL ERROR BAIL OUT
                fatalError(sockNum, DataFile);
                return 1;
            }
            if (data_block.block_num != ack_block_cntr) {// The incoming block number is not what we have
                // expected, i.e. this is a DATA pkt but the block number
                // in DATA was wrong (not last ACK’s block number + 1)
                // FATAL ERROR BAIL OUT
                printf("FLOWERROR: bad block number\n");
                fatalError(sockNum, DataFile);
                return 1;
            }
        } while (false);
        timeoutExpiredCount = 0;
        if ((last_write_size =fwrite(data_block.data, 1, (unsigned int)(recvMsgSize -4), DataFile)) < 0) {
            perror("TTFTP_ERROR:");
            fatalError(sockNum, DataFile);
            return 1;
        } else {
            printf("WRITING: %d\n",
                   recvMsgSize- HEADER_SIZE); //after printing to file if data size is smaller than 512 than file was successfully transmitted
        }
        if(last_write_size < (unsigned int)recvMsgSize - 4){
            printf("Error - fwrite() fails");
            fatalError(sockNum,DataFile);
            return 1;
        }

        // send ACK packet to the client
        ack_block.opcode= htons(4);
        ack_block.block_num=htons(ack_block_cntr);
        ack_block_cntr++;
        if(sendto(sockNum, &ack_block, sizeof(ack_block), 0, (struct sockaddr *)&clntAddr, sizeof(clntAddr)) != sizeof(ack_block)){ // check send all of packet correctly
            perror("TTFTP_ERROR:");
            fatalError(sockNum, DataFile);
            return 1;
        }
        else {//sending packet was successful, print that ACK packet was sent to client
            printf("OUT:ACK, %d\n", ack_block_cntr-1);
        }

    } while ( last_write_size == DATA_MAXSIZE); // Have blocks left to be read from client (not end of transmission)

    if(fclose(DataFile)) {
        perror("TTFTP_ERROR:");
    }
    clearSocket(sockNum);
    close(sockNum);
    return 0;
}

//********************************************
// function name: fatalError
// Description: handles the resources in case of a fatal error
// Parameters: socketNum- the number of the socket opened for communication
//				dataFile pointer to the file to which the data is written to.
// Returns: NONE
//**************************************************************************************
void fatalError(int socketNum, FILE* dataFile){
    clearSocket(socketNum);
    close(socketNum);
    if(fclose(dataFile)) {
        perror("TTFTP_ERROR:");
    }
}




//********************************************
// function name: clearSocket
// Description: clears data from the socket
// Parameters: ocketNum- the number of the socket opened for communication
// Returns: NONE
//**************************************************************************************
void clearSocket(int socketNum) {
    int bytesLeft = 0;
    if (ioctl(socketNum, FIONREAD, &bytesLeft) == -1) {
        perror("TTFTP_ERROR:");
    } else if (bytesLeft > 1) { //there are some bytes left for reading
        if(recv(socketNum, NULL, bytesLeft, 0) == -1 ){
            perror("TTFTP_ERROR:");
        }
    }
}
