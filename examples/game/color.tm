# Defines a struct used to represent colors using 64-bit floats (0.0 - 1.0),
# which can be used to draw colored rectangles in raylib
use <raylib.h>
use vectors

struct Color(r,g,b:Num32,a=1.0f32):
    RED := Color(1,0,0)
    GRAY := Color(.2f32,.2f32,.2f32)
    LIGHT_GRAY := Color(.7f32,.7f32,.7f32)

    func draw_rectangle(c:Color, pos:Vec2, size:Vec2):
        inline C {
            DrawRectangle(
                (int)($pos.$x), (int)($pos.$y), (int)($size.$x), (int)($size.$y),
                ((Color){
                    (int8_t)(uint8_t)(255.*$c.$r),
                    (int8_t)(uint8_t)(255.*$c.$g),
                    (int8_t)(uint8_t)(255.*$c.$b),
                    (int8_t)(uint8_t)(255.*$c.$a),
                })
            );
        }
