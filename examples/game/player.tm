# Defines a struct representing the player, which is controlled by WASD keys
use ./world.tm
use ./raylib.tm

struct Player(pos,prev_pos:Vector2):
    WALK_SPEED := Num32(500.)
    ACCEL := Num32(0.3)
    FRICTION := Num32(0.99)
    SIZE := Vector2(30, 30)
    COLOR := Color(0x60, 0x60, 0xbF)

    func update(p:@Player):
        target_x := inline C:Num32 {
            (Num32_t)((IsKeyDown(KEY_A) ? -1 : 0) + (IsKeyDown(KEY_D) ? 1 : 0))
        }
        target_y := inline C:Num32 {
            (Num32_t)((IsKeyDown(KEY_W) ? -1 : 0) + (IsKeyDown(KEY_S) ? 1 : 0))
        }
        target_vel := Vector2(target_x, target_y):norm() * Player.WALK_SPEED

        vel := (p.pos - p.prev_pos)/World.DT
        vel *= Player.FRICTION
        vel = vel:mix(target_vel, Player.ACCEL)

        p.prev_pos, p.pos = p.pos, p.pos + World.DT*vel

    func draw(p:Player):
        DrawRectangleV(p.pos, Player.SIZE, Player.COLOR)
