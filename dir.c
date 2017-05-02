#include<sys/stat.h>
#include<unistd.h>
#include<dirent.h>
#include<error.h>

/* Retro-Printer Directory Handler - Program by RWAP Software to tidy the directory used to store the temporary files
 * v1.0
 *
 * v1.0 First Proof of concept version
 */

int dirX(char *path)
{
    int i = 0;
 
    DIR *dp;
    struct dirent *files;

    if ((dp = opendir(path)) == NULL)
	return 0;
    while ((files = readdir(dp)) != NULL) {
	if (!strcmp(files->d_name, ".") || !strcmp(files->d_name, ".."))
	    continue;
	if (i < atoi(&files->d_name[4]))
	    i = atoi(&files->d_name[4]);
    }

    return i;
}

void reduce_pages(int page, char *path)
{
    int i = 0;
    char command[1000];
    DIR *dp;
    struct dirent *files;

    if (page > 199) {
	for (i = 0; i <= 100; i++) {
	    sprintf(command, "rm %spage%d.bmp;rm %s%d.%s;rm %spage%d.pdf", path, i, path, i,"raw", path, i);
	    system(command);
	}
	for (i = i; i <= page; i++) {
	    sprintf(command,
		    "mv %spage%d.bmp %spage%d.bmp; mv %s%d.%s %s%d.%s;mv %spage%d.pdf %spage%d.pdf;",path, i, path, i - 101, path, i,"raw", path, i - 101,"raw", path, i,path, i - 101);
	    system(command);
	}
	page = dirX(path);
	reduce_pages(page, path);
    }
}
