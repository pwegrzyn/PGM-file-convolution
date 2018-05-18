/* Systemy Operacyjne 2018 Patryk Wegrzyn */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>
#include <math.h>

// Name of the raports file
#define RAPORTS_FILE_NAME "Times.txt"

// Represents a PGM image in memory
typedef struct pgm_image_tag{
    int width;
    int heigth;
    int max_grey;
    unsigned int *array;
} pgm_image;

// Represents a Filter matrix in memory
typedef struct filter_matrix_tag{
    int size;
    double *array;
} filter_matrix;

// Represents data passed to the thread task
typedef struct thread_task_data_tag{
    int start;
    int length;
    pgm_image *img;
    filter_matrix *fil;
    pgm_image *filtered_img;
} thread_task_data;

// Helper function used to signalize argument errors
void sig_arg_err()
{
    printf("Wrong argument format.\n"
           "Usage: zad1 [no_threads] [in_file] [filter_file] [out_file]\n");
    exit(EXIT_FAILURE);
}

// Fetches the Thread ID of the current thread
long gettid(void)
{
    return syscall(SYS_gettid);
}

// Variadic function used to printf a message to stdout and another file simultanously
void tee(FILE *f, char const *msg, ...)
{     
    va_list ap;
    va_start(ap, msg);
    vprintf(msg, ap);
    va_end(ap);
    va_start(ap, msg);
    vfprintf(f, msg, ap);
    va_end(ap);
}

// Used for time measurements
long clctd(struct timeval *start, struct timeval *end)
{
    return (long)((end->tv_sec * 1000000 + end->tv_usec) - (start->tv_sec * 1000000 + start->tv_usec));
}

// Skips whitespace characters and / or comments
void skip(FILE *file)
{
    char c, comment[128];
    while((c=fgetc(file)) != EOF && isspace(c));
    if(c == '#')
    {
        fgets(comment, sizeof(comment), file);
        skip(file);
    }
    else
    {
        fseek(file, -1, SEEK_CUR);
    }
}

// Reads a PGM image from the disk to memory
pgm_image* read_pgm(const char *path)
{
    int width;
    int heigth;
    int max_grey;
    int size;
    FILE *input_file;
    pgm_image *img;
    char magic_number[3];

    if((input_file=fopen(path, "r")) == NULL)
    {
        perror("Error while opening PGM file");
        exit(EXIT_FAILURE);
    }

    fgets(magic_number, sizeof(magic_number), input_file);
    if(strcmp(magic_number, "P5") && strcmp(magic_number, "P2"))
    {
        fprintf(stderr, "The PGM file has an invalid magic number\n");
        exit(EXIT_FAILURE);
    }

    skip(input_file);
    fscanf(input_file, "%d", &width);
    skip(input_file);
    fscanf(input_file, "%d", &heigth);
    skip(input_file);
    fscanf(input_file, "%d", &max_grey);
    skip(input_file);

    img = (pgm_image*)malloc(sizeof(pgm_image));
    if(img == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }

    img->heigth = heigth;
    img->width = width;
    img->max_grey = max_grey;
    size = heigth * width;
    img->array = (unsigned int*)malloc(sizeof(unsigned int*) * size);
    if(img->array == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }

    char buffer[80];
    int counter = 0;
    while(fgets(buffer, 80, input_file) != NULL)
    {
        char *token = strtok(buffer, " \n");
        while(token)
        {
            sscanf(token, "%d", &(img->array[counter]));
            token = strtok(NULL, " \n");
            counter++;
        }
    }

    fclose(input_file);
    return img;
}

// Saves a given PGM image to the disk
void save_pgm(pgm_image *img, const char *out_file_name)
{
    FILE *output_file;
    int size;

    if((output_file=fopen(out_file_name, "w")) == NULL)
    {
        perror("Error while opening PGM file");
        exit(EXIT_FAILURE);
    }

    fprintf(output_file, "P2\n%d %d\n%d\n", img->width, img->heigth, img->max_grey);

    size = img->heigth * img->width;
    char newline = '\n';
    for(int i = 0; i < size; i++)
    {
        char buffer[4];
        if((i + 1) % 17 == 0)
        {
            sprintf(buffer, "%3d", img->array[i]);
            fwrite(buffer, 1, 3 * sizeof(char), output_file);
            fwrite(&newline, 1, 1, output_file);
        }
        else
        {
            sprintf(buffer, "%3d ", img->array[i]);
            fwrite(buffer, 1, sizeof(buffer), output_file);
        }
    }

    fclose(output_file);
}

// Clears the PGM image structure
void destroy_pgm(pgm_image *img)
{
    free(img->array);
    free(img);
    return;
}

// Reads a filter matrix from a file on disk to memory
filter_matrix* read_filter(const char *path)
{
    int size;
    FILE *input_file;
    filter_matrix *fil;
    char buffer[10];

    if((input_file=fopen(path, "r")) == NULL)
    {
        perror("Error while opening Filter file");
        exit(EXIT_FAILURE);
    }

    fil = (filter_matrix *)malloc(sizeof(filter_matrix));
    if(fil == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }
    
    fgets(buffer, 10, input_file);
    sscanf(buffer, "%d", &size);
    fil->size = size;

    fil->array = (double*)malloc(sizeof(double) * size * size);
    if(fil->array == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }
    
    char buffer2[1024];
    int counter = 0;
    while(fgets(buffer2, 1024, input_file) != NULL)
    {
        char *token = strtok(buffer2, " \n");
        while(token)
        {
            sscanf(token, "%lf", &(fil->array[counter]));
            token = strtok(NULL, " \n");
            counter++;
        }
    }

    fclose(input_file);
    return fil;
}

// Generates a random filter matrix
filter_matrix* generate_filter(int size)
{
    int size_squared = size * size;
    filter_matrix *fil = (filter_matrix*)malloc(sizeof(filter_matrix));
    if(fil == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }

    fil->array = (double*)malloc(sizeof(double) * size_squared);
    fil->size = size;

    double sum = 0;
    for(int i = 0; i < size_squared; i++)
    {
        fil->array[i] = (double)rand()/RAND_MAX;
        sum += fil->array[i];
    }
    for(int i = 0; i < size_squared; i++)
    {
        fil->array[i] /= sum;
    }

    return fil;
}

// Saves a given Filter Matrix to the disk
void save_filter(filter_matrix *fil, const char *out_file_name)
{
    FILE *output_file;
    char buffer [10];
    int size_squared = fil->size * fil->size;

    if((output_file=fopen(out_file_name, "w")) == NULL)
    {
        perror("Error while opening filter file");
        exit(EXIT_FAILURE);
    }

    fprintf(output_file, "%d\n", fil->size);

    for(int i = 0; i < size_squared; i++)
    {
        sprintf(buffer, "%f ", fil->array[i]);
        fputs(buffer, output_file);
        if((i + 1) % fil->size == 0)
        {
            fprintf(output_file, "\n");
        }
    }

    fclose(output_file);
}

// Clears the Filter Matrix structure
void destroy_filter(filter_matrix *fil)
{
    free(fil->array);
    free(fil);
}

// Copies an PGM image in memory
pgm_image* copy_img(pgm_image *original)
{
    pgm_image *copy = (pgm_image*)malloc(sizeof(pgm_image));
    if(copy == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    } 
    copy->heigth = original->heigth;
    copy->width = original->width;
    copy->max_grey = original->max_grey;
    copy->array = (unsigned int*)malloc(sizeof(unsigned int) * copy->width * copy->heigth);
    if(copy->array == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }
    return copy;
}

// Helper function
int min(int a, int b)
{
    return a < b ? a : b;
}

// Helper function
int max(int a, int b)
{
    return a > b ? a : b;
}

// Start function of each auxilliary thread, filters a portion of the image
void* thread_filter_portion(void *args)
{
    pgm_image *img = ((thread_task_data*)args)->img;
    filter_matrix *fil = ((thread_task_data*)args)->fil;
    pgm_image *filtered_img = ((thread_task_data*)args)->filtered_img;
    int start = ((thread_task_data*)args)->start;
    int length = ((thread_task_data*)args)->length;

    int fil_size_squared = fil->size * fil->size;

    for(int i = start; i < start + length; i++)
    {
        double sum = 0;
        int x_index_outer = i % img->width;
        int y_index_outer = i / img->width;
        for(int j = 0; j < fil_size_squared; j++)
        {
            int x_index_fil = j % fil->size;
            int y_index_fil = j / fil->size;
            int x_index_inner = min(img->width-1, max(0, x_index_outer - (int)ceil(fil->size / 2) + x_index_fil));
            int y_index_inner = min(img->heigth-1, max(0, y_index_outer - (int)ceil(fil->size / 2) + y_index_fil));
            int index = y_index_inner * img->width + x_index_inner;
            sum += img->array[index] * fil->array[j];
        }
        filtered_img->array[i] = (int)round(sum);
    }

    return NULL;
}

// MAIN function
int main(int argc, char **argv)
{
    int no_threads, pixels_per_thread;
    const char *input_file_name, *filter_file_name, *output_file_name;
    struct timeval start_r, end_r;
    const char *raports_file_name;
    FILE *raport_file;
    pgm_image *img, *filtered_img;
    filter_matrix *fil;
    pthread_t *threads;
    thread_task_data **tasks_data;

    if (argc != 5) sig_arg_err();
    else 
    {
        no_threads = (int)strtol(argv[1], NULL, 10);
        input_file_name = argv[2];
        filter_file_name = argv[3];
        output_file_name = argv[4];
    }

    raports_file_name = RAPORTS_FILE_NAME;
    raport_file = fopen(raports_file_name, "a");
    if (raport_file == NULL)
    {
        perror("Error while opening raports file");
        exit(EXIT_FAILURE);
    }
    
    img = read_pgm(input_file_name);
    fil = read_filter(filter_file_name);
    filtered_img = copy_img(img);
    pixels_per_thread = (img->heigth * img->width) / no_threads;

    threads = (pthread_t*)malloc(sizeof(pthread_t) * no_threads);
    if(threads == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }

    tasks_data = (thread_task_data**)malloc(sizeof(thread_task_data*) * no_threads);
    if(tasks_data == NULL)
    {
        perror("Error while allocating memory");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));

    gettimeofday(&start_r, NULL);

    for(int i = 0; i < no_threads; i++)
    {
        tasks_data[i] = (thread_task_data*)malloc(sizeof(thread_task_data));
        if(tasks_data[i] == NULL)
        {
            perror("Error while allocating memory");
            exit(EXIT_FAILURE);
        }

        tasks_data[i]->start = i * pixels_per_thread;
        tasks_data[i]->length = pixels_per_thread;
        tasks_data[i]->img = img;
        tasks_data[i]->fil = fil;
        tasks_data[i]->filtered_img = filtered_img;
        
        if(pthread_create(&threads[i], NULL, (void *)thread_filter_portion, (void*)tasks_data[i]) != 0)
        {
            fprintf(stderr, "Error while creating a new thread\n");
            exit(EXIT_FAILURE);
        }
    }

    for(int i = 0; i < no_threads; i++)
    {
        int err_code;
        if((err_code=pthread_join(threads[i], NULL)) != 0)
        {
            fprintf(stderr, "Error while joining with thread: %s\n", strerror(err_code));
            exit(EXIT_FAILURE);
        }
        free(tasks_data[i]);
    }

    gettimeofday(&end_r, NULL);

    save_pgm(filtered_img, output_file_name);

    tee(raport_file, "The filtering process of the file %s with the filter %s"
        " using %d threads took: %ld us\n", input_file_name, filter_file_name,
        no_threads, clctd(&start_r, &end_r));
    
    free(tasks_data);
    free(threads);
    destroy_filter(fil);
    destroy_pgm(filtered_img);
    destroy_pgm(img);
    fclose(raport_file);

    return EXIT_SUCCESS;
}