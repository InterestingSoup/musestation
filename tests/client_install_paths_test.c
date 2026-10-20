#include "client_install_paths.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct check { const char *expected; int calls; };
static int probe(const char *path,void *arg){struct check *c=arg;c->calls++;return strcmp(path,c->expected);}
int main(void){
    struct muse_path_locator loc={.direct="/download0/muse/artifacts/j1.pkg",.relative="/muse/artifacts/j1.pkg",.fsid={1,2,3,4},.has_mount=1};
    struct muse_mount_record mounts[3]={
        {.fsid={9,9,9,9},.path="/unrelated"},
        {.fsid={1,2,3,4},.path="/mounted/runtime-discovered-instance"},
        {.fsid={1,2,3,4},.path="/mounted/second-alias"}};
    struct check check={"/mounted/runtime-discovered-instance/muse/artifacts/j1.pkg",0};char output[1024];
    assert(!muse_path_resolve_records(&loc,mounts,3,probe,&check,output,sizeof output));
    assert(!strcmp(output,check.expected)&&check.calls==2);
    check.calls=0;check.expected="/never-matches";
    assert(muse_path_resolve_records(&loc,mounts,3,probe,&check,output,sizeof output)&&check.calls==3);
    strcpy(loc.relative,"/../other-title/file");check.calls=0;
    assert(muse_path_resolve_records(&loc,mounts,3,probe,&check,output,sizeof output)&&check.calls==1);
    assert(!muse_path_describe("/local/file.pkg","/explicit/helper/file.pkg",&loc)&&!loc.has_mount);
    check.expected="/explicit/helper/file.pkg";check.calls=0;
    assert(!muse_path_resolve_records(&loc,mounts,3,probe,&check,output,sizeof output)&&check.calls==1);
    assert(muse_path_describe("/local/file.pkg","/invalid/../file.pkg",&loc));
    puts("mount-ID namespace mapping, probe rejection, explicit path and traversal tests passed");
}
