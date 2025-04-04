# This game demo uses Raylib to present a simple maze-type game
use ./raylib.tm
use ./world.tm

func main(map=(./map.txt)):
    InitWindow(1600, 900, CString("raylib [core] example - 2d camera"))

    map_contents := map:read() or exit("Could not find the game map: $map")

    world := @World(
        player=@Player(Vector2(0,0), Vector2(0,0)),
        goal=@Box(Vector2(0,0), Vector2(50,50), color=Color(0x10,0xa0,0x10)),
        boxes=@[],
    )
    world:load_map(map_contents)

    SetTargetFPS(60)

    while not WindowShouldClose():
        dt := GetFrameTime()
        world:update(dt)

        BeginDrawing()
        ClearBackground(Color(0xCC, 0xCC, 0xCC, 0xFF))
        world:draw()
        EndDrawing()
    
    CloseWindow()

