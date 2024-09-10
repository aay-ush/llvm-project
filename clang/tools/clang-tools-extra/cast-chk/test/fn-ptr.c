typedef void(*CMPF)(void*, void*);

void fInt2(void *p, void *q) {
    int *pi = (int*) p;
    int *qi = (int*) q;
    *pi = *pi + 1;
    *qi = *qi + 2;
}

void fInt(void *p, void *q) {
    int *pi = (int*) p;
    int *qi = (int*) q;
    *pi = *pi + 1;
    *qi = *qi + 2;
    fInt2(pi, qi);
}

void hof(void *p, void *q, CMPF f) {
    //f(p, q);
    fInt(p, q);
}

int main() {
    int i = 1;
    int j = 2;
    int *pi = &i;
    int *qi = &j;

    //fInt(pi, qi);
    //CMPF f = fInt;
    //f(pi, qi);

    hof(pi, qi, fInt);

    return 0;
}
