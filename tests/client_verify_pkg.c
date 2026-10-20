/* Read-only verifier for an actual package built by the pinned packager. */
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    if(argc!=6){fprintf(stderr,"usage: verify-pkg PATH TITLE_ID CONTENT_ID SHA256 SIZE\n");return 2;}
    struct muse_config cfg;muse_config_defaults(&cfg);struct muse_client client;muse_client_init(&client,&cfg);struct muse_build_status status={0};
    strcpy(status.state,"done");strcpy(status.kind,"pkg");
    if(strlen(argv[2])>=sizeof(status.title_id)||strlen(argv[3])>=sizeof(status.content_id)||strlen(argv[4])!=64)return 2;
    strcpy(status.title_id,argv[2]);strcpy(status.content_id,argv[3]);strcpy(status.sha256,argv[4]);char *end=NULL;status.size=strtoull(argv[5],&end,10);if(!end||*end)return 2;
    if(muse_client_verify_package(&client,argv[1],&status)){fprintf(stderr,"%s\n",client.error);return 1;}puts("Actual FIH package identity, extents, length and SHA256 verified by gadget client");return 0;
}
