typedef struct { double x, y; } Point;
typedef struct { double w, h; } Size;
typedef struct { Point origin; Size size; } Rect;
typedef struct { double a, b, c, d, tx, ty; } Transform;
extern const Point CGPointZero;
extern const Rect CGRectZero, CGRectNull;
extern const Transform CGAffineTransformIdentity;
extern double CGRectGetWidth(Rect), CGRectGetHeight(Rect);
extern Rect CGRectOffset(Rect, double, double);
extern _Bool CGRectEqualToRect(Rect, Rect);
extern Transform CGAffineTransformScale(Transform, double, double);

int main(void)
{
    if (CGPointZero.x || CGPointZero.y || CGRectZero.origin.x || CGRectZero.origin.y ||
        CGRectZero.size.w || CGRectZero.size.h) return 1;
    if (CGRectNull.origin.x != __builtin_inf() || CGRectNull.origin.y != __builtin_inf() ||
        CGRectNull.size.w || CGRectNull.size.h) return 2;
    Transform identity = CGAffineTransformIdentity;
    if (identity.a != 1 || identity.b || identity.c || identity.d != 1 || identity.tx || identity.ty) return 3;
    Rect r = {{12, -7}, {-40, -60}};
    if (CGRectGetWidth(r) != 40 || CGRectGetHeight(r) != 60) return 4;
    Rect moved = CGRectOffset(r, -3, 9);
    if (moved.origin.x != 9 || moved.origin.y != 2 || moved.size.w != -40 || moved.size.h != -60) return 5;
    if (!CGRectEqualToRect(moved, (Rect){{9,2},{-40,-60}}) || CGRectEqualToRect(r, moved)) return 6;
    if (!CGRectEqualToRect(CGRectNull, CGRectOffset(CGRectNull, __builtin_inf(), -__builtin_inf()))) return 7;
    if (CGRectEqualToRect(CGRectZero, CGRectNull)) return 8;
    if (CGRectEqualToRect(r, (Rect){{-28,-67},{40,60}})) return 9;
    Transform source = {2, 3, 4, 5, 17, -19};
    Transform result = CGAffineTransformScale(source, -2, 0.5);
    if (result.a != -4 || result.b != -6 || result.c != 2 || result.d != 2.5 || result.tx != 17 || result.ty != -19) return 10;
    if (source.a != 2 || source.d != 5) return 11;
    result = CGAffineTransformScale(identity, 0, -3);
    if (result.a || result.b || result.c || result.d != -3 || result.tx || result.ty) return 12;
    Rect negative_zero = {{0,0},{-0.0,-0.0}};
    if (__builtin_signbit(CGRectGetWidth(negative_zero)) || __builtin_signbit(CGRectGetHeight(negative_zero))) return 13;
    Rect nan_rect = {{0,0},{__builtin_nan(""),1}};
    if (CGRectEqualToRect(nan_rect, nan_rect) || !__builtin_isnan(CGRectGetWidth(nan_rect))) return 14;
    return 0;
}
