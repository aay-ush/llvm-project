#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int lines;
    int cols;
    Point* points;
} State;

static State st;

static void draw() {
    Point p, q;
    memcpy(&p, &st.points[0], sizeof p);

    printf("p = {%d, %d}\n", p.x, p.y);
}

int main() {
    st.points = calloc(2, sizeof(Point));
    st.lines = 0;
    st.cols = 1;
    st.points[0].x = 0;
    st.points[0].y = 0;
    st.points[1].x = 0;
    st.points[1].y= 1;

    return 0;
}
