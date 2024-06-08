void f(void*p) {
    int *pi = (int*)p;
    *pi = 1;
    void *pv = (void*)pi;
    short *ps = (short*)pv;
    *ps = 2;
}

void f2(void* p) {
    int *pi = (int*)p;
    int *pi2 = pi;
    void *pv = (void*)pi2;
    short *ps = (short*)pv;
    *ps = 3;
}

int main() {
    int i = 1;
    f(&i);

    void *p;
    p = (void*) &i;

    void (*fp) (void*) = f;
    fp(p);

    f2(&i);

    return i;
}

