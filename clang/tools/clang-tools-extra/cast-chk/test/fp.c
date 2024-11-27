void f(void *f_pv) {
    char *xp = (char*) f_pv;
}

void g(void *g_pv) {
    int *yp = (int*) g_pv;
}

void hof(void *x, void (*p)(void*)) {
    p(x);
}

void h(void *f_pv) {
    int *zp = (int*) f_pv;
}

int main() {
    int i = 0;
    int *pi = &i;

    void (*fp)(void*) = f;
    //fp(pi);

    f(pi);

    hof(pi, fp);
    //fp = h;
    //hof(pi, fp);

    void (*fg)(void*) = g;
    hof(pi, g);

    return 0;
}

