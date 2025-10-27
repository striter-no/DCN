#include <allocator.h>

int main(){
    struct allocator alloc;
    allocator_init(&alloc);

    int *data = alc_malloc(&alloc, 100 * sizeof(int));
    char *str = alc_calloc(&alloc, 50, sizeof(char));

    data = alc_realloc(&alloc, data, 200 * sizeof(int));

    alc_free(&alloc, str);

    allocator_end(&alloc);
}
