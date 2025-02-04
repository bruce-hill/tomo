# Defines a struct used to represent colors using 64-bit floats (0.0 - 1.0),
# which can be used to draw colored rectangles in raylib
use <raylib.h>
use vectors

struct Color(r,g,b:Num,a=1.0):
    PLAYER := Color(.1,.1,.6,1.)
    GRAY := Color(.4,.4,.4)
    LIGHT_GRAY := Color(.7,.7,.7)
    GOAL := Color(.1,.5,.0)

    func draw_rectangle(c:Color, pos:Vec2, size:Vec2):
        inline C {
            DrawRectangle(
                (int)(_$pos.$x), (int)(_$pos.$y), (int)(_$size.$x), (int)(_$size.$y),
                ((Color){
                    (int8_t)(uint8_t)(255.*_$c.$r),
                    (int8_t)(uint8_t)(255.*_$c.$g),
                    (int8_t)(uint8_t)(255.*_$c.$b),
                    (int8_t)(uint8_t)(255.*_$c.$a),
                })
            );
        }
