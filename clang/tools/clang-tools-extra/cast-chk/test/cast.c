void f(void*p) {
    int *pi = (int*)p;
    *pi = 1;
}

int main() {
    int i = 1;
    f(&i);

    void *p;
    p = (void*) &i;

    void (*fp) (void*) = f;
    fp(p);

    return i;
}

