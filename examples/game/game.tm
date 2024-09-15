# This game demo uses Raylib to present a simple 
use libraylib.so
use <raylib.h>
use <raymath.h>

use ./world.tm

func main(map=(./map.txt)):
    extern InitWindow:func(w:Int32, h:Int32, title:CString)->Void
    InitWindow(1600, 900, "raylib [core] example - 2d camera")

    map_contents := if contents := map:read():
        contents
    else: exit(code=1, "Could not find the game map: $map")

    World.CURRENT:load_map(map_contents)

    extern SetTargetFPS:func(fps:Int32)
    SetTargetFPS(60)

    extern WindowShouldClose:func()->Bool

    while not WindowShouldClose():
        extern GetFrameTime:func()->Num32
        dt := GetFrameTime()
        World.CURRENT:update(Num(dt))

        extern BeginDrawing:func()
        BeginDrawing()

        inline C {
            ClearBackground((Color){0xCC, 0xCC, 0xCC, 0xFF});
        }

        World.CURRENT:draw()

        extern EndDrawing:func()
        EndDrawing()
    
    extern CloseWindow:func()
    CloseWindow()

