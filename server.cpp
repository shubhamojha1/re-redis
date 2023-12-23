#include <stdio.h>

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0){
        die("socket()");
    }
}