void fg(void*p, void* q) {
}

void f(void*f_p) {   // f = f.p: void* -> int* ->...
    int *f_pi = (int*)f_p;
    *f_pi = 1;
    void *f_pv = (void*)f_pi;
    short *f_ps = (short*)f_pv;
    *f_ps = 2;
}

void f2(void* f2_p) {
    int *f2_pi = (int*)f2_p;
    int *f2_pi2 = f2_pi;
    void *f2_pv = (void*)f2_pi2;
    short *f2_ps = (short*)f2_pv;
    *f2_ps = 3;
    f(f2_p);
}

int main() {
    int i = 1;
    f(&i);

    void *p;
    p = (void*) &i;

    void (*fnp) (void*) = f;
    fnp(p);

    f2(&i);

    return i;
}

