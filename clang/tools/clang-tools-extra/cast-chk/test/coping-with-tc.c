typedef struct {
    int x, y;
} Point;

typedef enum {
    RED, BLUE
} color;

typedef struct {
    int x, y;
    color c;
} ColorPoint;

typedef struct {
    Point p;
    color c;
} ColorPoint2;

void translateX(Point *p, int dx) {
    p->x += dx;
}

int main() {
    Point pt = {0, 0};
    ColorPoint cpt = {0, 1, RED};

    translateX(&pt, 1);
    translateX((Point *) &cpt, 1);

    ColorPoint2 cp = {{0, 1}, BLUE};
    translateX((Point *) &cp, 1);
    translateX(&(cp.p), 1);


    return 0;
}
