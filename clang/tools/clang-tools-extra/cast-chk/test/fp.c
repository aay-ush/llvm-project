void f(void *f_pv) {
}

void g(void *g_pv) {
}

void hof(void *x, void (*p)(void*)) {
    p(x);
}

int main() {
    int i = 0;
    int *pi = &i;

    void (*fp)(void*) = f;
    fp(pi);

    hof(pi, fp);

    void (*fg)(void*) = g;
    hof(pi, fg);

    return 0;
}

