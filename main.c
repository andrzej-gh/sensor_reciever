#include <stdio.h>
#include <stdlib.h>
#include <json-c/json.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#define DEF_PORT 5200

typedef struct Reciever {
   int recvId;
   int active;
   int sensor;
} Reciever;

int editInProgress;

/* Reciever functions */


void
Reciever_getRecieverData(Reciever *reciever, struct json_object *jRecv)
{
   struct json_object *jRecvParam;
   /* Sensor ID */
   jRecvParam = json_object_object_get(jRecv, "ID");
   reciever->sensor = json_object_get_int(jRecvParam);
   /* Is Reciever Active */
   jRecvParam = json_object_object_get(jRecv, "Active");
   reciever->active = json_object_get_int(jRecvParam);
}


void
Reciever_print(Reciever *reciever) {
   printf("Reciever ID:%d monitoring sensor ID:%d, status %s\n", reciever->recvId,
          reciever->sensor, (reciever->active > 0) ? "active" : "not active");
   fflush(stdout);
}


void
Reciever_printMsg(char *msg, int msgLen)
{
   int j, field = 0;
   char id[32];
   char type[32];
   char value[32];
   char quality[32];
   char outMsg[1024];
   static const char red[] = "\033[31m";
   static const char green[] = "\033[32m";
   static const char yellow[] = "\033[33m";
   static const char defC[] = "\033[0m";

   /* $FIX, id, type, value, quality* */
   msg += 6;
   while (field < 4) {
      for (j = 0; j < 32; j++) {
         if (msg[j] == ',' || msg[j] == '*') {
            switch (field) {
            case 0: //ID
               strncpy(id, msg, j);
               id[j] = 0;
               break;
            case 1: // type
               strncpy(type, msg, j);
               type[j] = 0;
               break;
            case 2: // value
               strncpy(value, msg, j);
               value[j] = 0;
               break;
            case 3: // quality
               strncpy(quality, msg, j);
               quality[j] = 0;
               break;
            }
            field++;
            msg += j + 2;
            break;
         }
      }
   }

   sprintf(outMsg, "Recieved data from %s sensor ID=%s, value=%s, quality=%s",
           type, id, value, quality);
   if (quality[0] == 'A') {
      printf("%s%s%s\n", red, outMsg, defC);
   } else if (quality[0] == 'W') {
      printf("%s%s%s\n", yellow, outMsg, defC);
   } else {
      printf("%s%s%s\n", green, outMsg, defC);
   }
}


/* Worker threads */


void *
reciever(void *data)
{
   Reciever *reciever = (Reciever *) data;
   struct sockaddr_in servAddr;
   int socketFd, ret, len;
   char recvBuf[1024];

   memset(recvBuf, 0, sizeof(recvBuf));

   memset(&servAddr, 0, sizeof(servAddr));
   servAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
   servAddr.sin_port = htons(DEF_PORT + reciever->sensor);
   servAddr.sin_family = AF_INET;

   socketFd = socket(AF_INET, SOCK_STREAM, 0);

   printf("Created reciever for sensor with ID=%d, status %s\n",
          reciever->sensor, (reciever->active > 0) ? "active" : "not active");
   fflush(stdout);

   ret = connect(socketFd, (struct sockaddr *)&servAddr, sizeof(servAddr));
   if (ret < 0) {
      printf("Connection failed\n");
      exit(1);
   }

   while (1) {
      len = read(socketFd, recvBuf, sizeof(recvBuf) -1);
      if (len < 0) {
         printf("Read error\n");
         exit(1);
      }

      recvBuf[len] = 0;

      if(reciever->active > 0 && editInProgress == 0) {
         Reciever_printMsg(recvBuf,len);
         fflush(stdout);
      }
   }
}


int main(int argc, char *argv[])
{
   struct json_object *jRecvsConfig, *jRecvs, *jRecv;
   int i, sensorId, len;
   char configFile[32];
   Reciever *recievers;
   pthread_t *clientThreads;
   int s = 0, c = 0;
   char option = ' ';
   int recvId;

   editInProgress = 0;

   /* parse input */
   for (i = 1; i < argc; i++) {
      if (i + 1 == argc || argv[i][0] != '-') {
	     printf("Invalid input\n");
	     exit(1);
      }

      switch (argv[i][1]) {
      case 's':
         sensorId = atoi(argv[++i]);
         s = 1;
         break;
      case 'c':
	     strncpy(configFile, argv[++i], 31);
	     c = 1;
	     break;
      default:
	     printf("Unknown option\n");
	     exit(1);
      }
   }

   s = 1;
   sensorId = 1;

   if (c == 1 && s == 1) {
      printf("ERROR: Please run program with only one flag\n");
      exit(1);
   }

   if (c == 0 && s == 0) {
      printf("ERROR: Please provide input:\n"
             "-c : configFile with recievers data\n"
             "-s : sensor to listen\n"
             "NOTE: use only one flag\n");
      exit(1);
   }

   if (c == 1) {
      /* Parse json file */
      jRecvsConfig = json_object_from_file(configFile);
      jRecvs = json_object_object_get(jRecvsConfig, "Recievers");
      len = json_object_array_length(jRecvs);
      recievers = malloc(len * sizeof(Reciever));
      for (i = 0; i < len; i++) {
         memset(&recievers[i], 0, sizeof(Reciever));
         jRecv = json_object_array_get_idx(jRecvs, i);
         Reciever_getRecieverData(&recievers[i], jRecv);
         recievers[i].recvId = i;
      }
   } else {
      len = 1;
      recievers = malloc(len * sizeof(Reciever));
      recievers[0].sensor = sensorId;
      recievers[0].active = 1;
      recievers[0].recvId = 0;
   }

   clientThreads = malloc(len * sizeof(pthread_t));
   for (i = 0; i < len; i++) {
      pthread_create(&clientThreads[i], NULL, reciever, &recievers[i]);
   }

   while (1) {
      scanf("%c", &option);
      getchar(); //remove newline char from input buffer;
      if (option == 'e') {
         editInProgress = 1;
         printf("Menu:\n"
                "c - configure reciever\n"
                "l - list all recievers\n");
         scanf("%c", &option);
         getchar();
         if (option == 'l') {
            for (i = 0; i < len; i++) {
               Reciever_print(&recievers[i]);
            }
         } else if (option == 'c') {
            printf("Please specify reciever id\n");
            scanf("%d", &recvId);
            getchar();
            printf("Please specify option:\n"
                   "a - activate\n"
                   "d - deactivate\n");
            /* TODO: add more options like:
             * modify recievers sensor id, add new reciever, etc...
             */
            scanf("%c", &option);
            if (option == 'a') {
               recievers[recvId].active = 1;
            } else if (option == 'd') {
               recievers[recvId].active = 0;
            }
         }
      }
      sleep(1);
      editInProgress = 0;
   }

   /* Join all threads */
   for(i = 0; i < len; i++) {
      pthread_join(clientThreads[i], NULL);
   }

   free(recievers);
   free(clientThreads);

   return 0;
}
