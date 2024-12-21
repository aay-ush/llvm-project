#include <stdio.h>

struct X {
    int a;
};

struct YX {
    int a;
    int b;
};

struct ZX {
    int a;
    char b;
};

struct Y {
    struct X x;
    int b;
};

void f(void *pv, int type) {
    struct X *px = 0;
    struct YX * pyx = 0;
    struct ZX * pzx = 0;
    struct Y * py = 0;

    switch(type) {
        case 1:
            px = (struct X*) pv;
            void *pv2 = px;
            px->a++;
            break;

        case 2:
            pyx = (struct YX*) pv;
            void *pv3 = pyx;
            pyx->a++;
            pyx->b++;
            break;

        case 3:
            pzx = (struct ZX*) pv;
            void *pv4 = pzx;
            pzx->a++;
            pzx->b++;
            break;

        case 4:
            py = (struct Y*) pv;
            void *pv5 = py;
            py->x.a++;
            py->b++;
            break;
    }
}

void printX(void *p) {
    struct X *px = (struct X*) p;
    printf("{%d}", px->a);
}

int main() {
    struct X x = {0};
    struct YX yx = {1, 0};
    struct ZX zx = {2, 'z'};
    struct Y y = {{3}, 1};

    void * ps = &x;
    f(ps, 1);

    struct Y * py = &y;
    py->x = x;

    printX(&x);
    printX(&yx);
    printX(&zx);
    printX(&y);

    return 0;
}

