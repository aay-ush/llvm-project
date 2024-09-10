// 1. pv -> pi 
// 2. pi -> pvv

void f(void * pv) {
    int * pi = (int*) pv;
    *pi = *pi + 1;

    void *pvv = (void*) pi;
}

// 3. p -> pv triggers census.f
// during 1, p->pv is not in census.
// during 3, p is checked and is not found in census.
// So either, partial inserts are needed or traversal repeat is needed.
int main() {
    int i = 0;
    int * p = &i;
    f(p);

    return 0;
}
