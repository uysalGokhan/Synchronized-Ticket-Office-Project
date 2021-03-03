#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <errno.h>


/*
 * The struct for clients informations
 */
struct Client
{
    char name[64];
    int arrivalTime;
    int serviceTime;
    int seatNumber;
};

/*
 * This condition variable is used for blocking clients thread after creating until all clients are created.
 * The aim of this is to start all clients at same time and preventing the time difference between clients which is
 * formed from the thread creating time in main.
 */
pthread_cond_t  cond;    		/* condition to wait on / signal */
int numberOfCreatedClient = 0;	//	The number of created Clients
pthread_mutex_t cond_mtx;    	/* mutex for the above */

int numOfClients = 0;			//  The second  arguments of the configuration file which is number of clients.
int numberOfSeats = -1;			//	Seat number in room.
int seats[200];					//	Seat array for room
pthread_mutex_t mtx_seat;		//	Blocking concurrent process on seats array.

int outputFd=-1;				// File descriptor for output file
pthread_mutex_t output_mtx;		// blocking concurrent writing to output file


mqd_t mq_A;						// Teller A message queue for receiving messages from clients
pthread_mutex_t mtx_A;			// Teller A mutex for checking if teller A is busy or not

mqd_t mq_B;						// Teller B message queue for receiving messages from clients
pthread_mutex_t mtx_B;			// Teller B mutex for checking if teller A is busy or not

mqd_t mq_C;						// Teller C message queue for receiving messages from clients
pthread_mutex_t mtx_C;			// Teller C mutex for checking if teller A is busy or not

sem_t sem;						// Semaphore for clients queue
sem_t tellerOrder_sem;           // Semaphore for teeller starting order


/*
 * This function is teller thread function. It takes one argument which is the teller name "A,B,C"
 * In the function firstly we take argument and write to output file the name of the thread.
 * After this thread is waiting in mq_receive to take message from client.
 * Then when it takes message it process the message and split the message to client name, seat number, service time
 * After processing message it checks the seat array and find a appropriate seat number.
 * Then it sleeps service time and writes the seat number to output file.
 */
void* teller(void* p){
	/*
	 * Taking argument.
	 */
	char id = * (char*)p;
	char buffer[512];
	snprintf(buffer, sizeof(buffer), "Teller %c has arrived.\n", id);
	/*
	 * lock output mutex and write to output file then unlock the mutex.
	 */
	pthread_mutex_lock(&output_mtx);
	write(outputFd, buffer, strlen(buffer));
	pthread_mutex_unlock(&output_mtx);
	sem_post(&tellerOrder_sem);
	char delim[] = ",";						// Delimeter for splitting message
	int seatNum=-1;							// initialize seat number
	int serviceTime = -1;					// initialize service time
	char nameOfClient[64];					// initialize name
	int status;								// checking status of mq_receive function
	while(1){								// While loop for welcoming clients
	int failed = 1;							// int for find or not find value
		if(id == 'A'){		// if teller is A
			status = mq_receive(mq_A, buffer, sizeof(buffer), 0);	// Waiting for a message from client
			// After message comes it locks the its own mutex for preventing the other clients to send message to it
			pthread_mutex_lock(&mtx_A);
		} else if(id == 'B'){	// if teller is B
			status = mq_receive(mq_B, buffer, sizeof(buffer), 0);	// Waiting for a message from client
			// After message comes it locks the its own mutex for preventing the other clients to send message to it
			pthread_mutex_lock(&mtx_B);
		} else if(id == 'C'){	// if teller is C
			status = mq_receive(mq_C, buffer, sizeof(buffer), 0);	// Waiting for a message from client
			// After message comes it locks the its own mutex for preventing the other clients to send message to it
			pthread_mutex_lock(&mtx_C);
		}
		/*
		 * Splits the message
		 */
		char *ptr = strtok(buffer, delim);
		memcpy(nameOfClient, ptr, strlen(ptr)+1);
		for(int j = 0 ; j < 2 ; j++){
		   ptr = strtok(NULL, delim);
		   if(j == 0){
			   seatNum = atoi(ptr);
		   } else {
			   serviceTime = atoi(ptr);
		   }
		  }
		/*
		 * lock the seat mutex and finds a appropriate seat number for client If it can't find the failed value should be stay at 0.
		 */
		pthread_mutex_lock(&mtx_seat);
		if(seatNum <= numberOfSeats){	// if desired seat number is between the limits
            if(seats[seatNum-1] == 0){	// if desired seat is empty
                snprintf(buffer, sizeof(buffer), "%s requests seat %d, reserves seat %d. Signed by Teller %c.\n", nameOfClient, seatNum, seatNum, id);
                failed = 0;				// found a seat
                seats[seatNum-1] = 1;	// fill the seat
            } else {	// if desiret seat is not empty
                for(int i = 0 ; i < numberOfSeats ; i++){	// try to find first empty seat
                    if(seats[i]==0){	// if seat is empty
                        snprintf(buffer, sizeof(buffer), "%s requests seat %d, reserves seat %d. Signed by Teller %c.\n", nameOfClient, seatNum, i + 1, id);
                        failed = 0;		// found a seat
                        seats[i] = 1;	// fill the seat
                        break;			// break the for loop
                    }
                }
            }
		} else {	// if desired seat is out of limits.
            for(int i = 0 ; i < numberOfSeats ; i++){	// find first empty seat.
                    if(seats[i]==0){	// if seat is empty
                        snprintf(buffer, sizeof(buffer), "%s requests seat %d, reserves seat %d. Signed by Teller %c.\n", nameOfClient, seatNum, i + 1, id);
                        failed = 0;		// found seat
                        seats[i] = 1;	// fill seat
                        break;			// break for loop
                    }
                }
		}
		if(failed == 1){
			snprintf(buffer, sizeof(buffer), "%s requests seat %d, reserves None. Signed by Teller %c.\n", nameOfClient, seatNum, id);
		}
		pthread_mutex_unlock(&mtx_seat);	// unlocking seat mutex because the job is done.

		usleep(serviceTime * 1000);			// sleep until service time of client.


            pthread_mutex_lock(&output_mtx);			// locks output mutex to write output
            write(outputFd, buffer, strlen(buffer));	// write to output file
            pthread_mutex_unlock(&output_mtx);			// unlocks output mutex


		if(id == 'A'){									// if teller is A
			pthread_mutex_unlock(&mtx_A);				// unlock its own mutex
		} else if(id == 'B'){							// if teller is B
			pthread_mutex_unlock(&mtx_B);				// unlock its own mutex
		} else if(id == 'C'){							// if teller is C
			pthread_mutex_unlock(&mtx_C);				// unlock its own mutex
		}
		usleep(4000);									// sleep 4 ms for deterministic work.
		sem_post(&sem);									// posting sem, So one of the waiting client can takes semaphore
	}

  pthread_exit(0);
}

/*
 * This function is client thread function. It takes one argument and this argument is Client struct.
 * In the function firstly we take argument and prepare a message for sending to tellers.
 * Then it waits on condition variable.
 * After all clients comes this point the las client will broadcast and all clients starst their sleep until arrival times.
 * Then when a client wakes up it uses sem_wait on semaphore and waits until there is a resource in this semaphore.
 * After taking semaphore it sends message to teller which is not busy in this time.
 * And then client sleeps until its service time and then leave.
 */
void* client(void* p){
	/*
	 * Takes argument and prepare the message
	 */
	struct Client *args = (struct Client *)p;
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s,%d,%d", args->name, args->seatNumber, args->serviceTime);

	pthread_mutex_lock(&cond_mtx);						// locks cond mutex
	numberOfCreatedClient = numberOfCreatedClient + 1;	// increase created Client number
	if(numberOfCreatedClient < numOfClients){			// if created Client number is less than total number of clients
		pthread_cond_wait(&cond, &cond_mtx);			// wait on cond wait.
	} else {											// if created client number is not less then total number of clients.
        pthread_cond_broadcast(&cond);					// broadcast to al waiting threads on cond wait.
	}
	pthread_mutex_unlock(&cond_mtx);					// unlock cond mutex

	usleep(args->arrivalTime * 1000);					// Sleeps until arrival time
	sem_wait(&sem);										// waits for a semaphore
	if(pthread_mutex_trylock(&mtx_A) == 0){				// if Teller A is not busy
		mq_send(mq_A, buffer, sizeof(buffer), 0);		// send message to mq_A "Teller A"
		usleep(4000);									// sleep 4 ms to deterministic work
		pthread_mutex_unlock(&mtx_A);					// unlock mutex A.
	} else if(pthread_mutex_trylock(&mtx_B) == 0){		// if teller B is not busy
		mq_send(mq_B, buffer, sizeof(buffer), 0);		// send message to mq_B "Teller B"
		usleep(4000);									// sleep 4 ms to deterministic work
		pthread_mutex_unlock(&mtx_B);					// unlock Mutex B
	} else if(pthread_mutex_trylock(&mtx_C) == 0){		// if teller C is not busy
		mq_send(mq_C, buffer, sizeof(buffer), 0);		// send message to mq_C "Teller C"
		usleep(4000);									// sleep 4 ms to deterministic work
		pthread_mutex_unlock(&mtx_C);					// unlock Mutex B
	} else {
	}
	usleep(args->serviceTime * 1000);					// sleeps until service time
	pthread_exit(0);
}

/*
 * This is main function. Firstly initializing of all mutex and other things are done.
 * Then, it takes input and output file names from program arguments.
 * Then it reads room name and client number from input file.
 * Then it creates teller threads.
 * After creating teller threads it is reading input file and takes the clients informations.
 * Then it creates client threads.
 * After all clients created it joins all client threads and wait until they terminated.
 * Then it takes all 3 mutex for tellers and be sure all client services are done and terminates successfully.
 */
int main(int argc, char **argv) {
	/*
	 * initializing all objects
	 */
	pthread_mutex_init(&output_mtx, NULL);
	pthread_mutex_init(&cond_mtx,NULL);
	pthread_mutex_init(&mtx_seat,NULL);
	pthread_cond_init(&cond,NULL);
	pthread_mutex_init(&mtx_A,NULL);
	pthread_mutex_init(&mtx_B,NULL);
	pthread_mutex_init(&mtx_C,NULL);
	sem_init(&sem, 0, 3);
	sem_init(&tellerOrder_sem, 0, 0);
	struct mq_attr attr;
	/* initialize the queue attributes */
	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = 256;
	attr.mq_curmsgs = 0;
	mq_A = mq_open("/mq_A", O_CREAT | O_RDWR, 0644, &attr);
	mq_B = mq_open("/mq_B", O_CREAT | O_RDWR, 0644, &attr);
	mq_C = mq_open("/mq_C", O_CREAT | O_RDWR, 0644, &attr);

	char const* const fileName = argv[1];				// input file path
	char const* const outputFileName = argv[2];			// output file path
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;// Mode for open function
	char outputBuffer[128];								// output buffer
	outputFd = open(outputFileName, O_WRONLY | O_CREAT | O_TRUNC, mode);	// output file descriptor
    snprintf(outputBuffer, sizeof(outputBuffer), "Welcome to the Sync-Ticket!\n");	// preparing output msg
    /*
     * locks output mutex then write to output file and
     */
    pthread_mutex_lock(&output_mtx);
    write(outputFd, outputBuffer, strlen(outputBuffer));
    pthread_mutex_unlock(&output_mtx);
    /*
     * possible room names
     */
	const char* room1 = "OdaTiyatrosu";
	const char* room2 = "UskudarStudyoSahne";
	const char* room3 = "KucukSahne";
	FILE* file = fopen(fileName, "r"); // open input file for reading
	char line[256];
	char delim[] = ",";					// delimeter for parsing input lines
	char roomName[64];
	char numberOfClients[8];
    fgets(roomName, sizeof(roomName), file);				// takes room name
    fgets(numberOfClients, sizeof(numberOfClients), file);	// takes number of clients
    numOfClients = atoi(numberOfClients);
    /*
     * deciding seat number from room name
     */
    if(strcmp(room1, roomName)){
    	numberOfSeats = 60;
    } else if(strcmp(room2, roomName)){
    	numberOfSeats = 80;
    } else if(strcmp(room3, roomName)){
    	numberOfSeats = 200;
    }
    struct Client clients[numOfClients];	// initialize struct array for client information
    pthread_t tellers[3];
    pthread_t clientThreads[numOfClients];
    char startRoutine[3] = "ABC";

    for(int i = 0 ; i < 3 ; i++){		// Creating teller threads
    	pthread_create(&tellers[i], NULL, teller, &startRoutine[i]);
        sem_wait(&tellerOrder_sem);
    }

    /*
     * Gets clients informations from input file and fill the client array
     */
    for(int i = 0 ; i < numOfClients ; i++){
    	fgets(line, sizeof(line), file);
    	char *ptr = strtok(line, delim);
    	memcpy(clients[i].name, ptr, strlen(ptr)+1);
    	for(int j = 0 ; j < 3 ; j++){
    		ptr = strtok(NULL, delim);
    		if(j == 0){
    			clients[i].arrivalTime = atoi(ptr);
    		} else if(j == 1){
    			clients[i].serviceTime = atoi(ptr);
    		} else {
    			clients[i].seatNumber = atoi(ptr);
    		}
    	}
    }

    /*
     * creating client threads
     */
    for(int i = 0 ; i < numOfClients ; i++){
    	pthread_create(&clientThreads[i], NULL, client, &clients[i]);
    }

    /*
     * joining client threads and wait them until they terminate
     */
    for(int i = 0 ; i < numOfClients ; i++){
   	    	pthread_join(clientThreads[i], NULL);
   	    }

    /*
     * check if all services is done
     */
    pthread_mutex_lock(&mtx_A);
    pthread_mutex_lock(&mtx_B);
    pthread_mutex_lock(&mtx_C);

    /*
     * prints final sentence and terminates successfully
     */
    snprintf(outputBuffer, sizeof(outputBuffer), "All clients received service.\n");
    pthread_mutex_lock(&output_mtx);
    write(outputFd, outputBuffer, strlen(outputBuffer));
    pthread_mutex_unlock(&output_mtx);
    close(outputFd);
    exit(0);
}

