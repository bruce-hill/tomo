# Defines a struct representing the player, which is controlled by WASD keys
use libraylib.so
use <raylib.h>
use <raymath.h>

use vectors
use ./world.tm

struct Player(pos,prev_pos:Vec2):
    WALK_SPEED := 500.
    ACCEL := 0.3
    FRICTION := 0.99
    SIZE := Vec2(30, 30)

    func update(p:@Player):
        target_x := inline C:Num {
            (Num_t)((IsKeyDown(KEY_A) ? -1 : 0) + (IsKeyDown(KEY_D) ? 1 : 0))
        }
        target_y := inline C:Num {
            (Num_t)((IsKeyDown(KEY_W) ? -1 : 0) + (IsKeyDown(KEY_S) ? 1 : 0))
        }
        target_vel := Vec2(target_x, target_y):norm() * Player.WALK_SPEED

        vel := (p.pos - p.prev_pos)/World.DT
        vel *= Player.FRICTION
        vel = vel:mix(target_vel, Player.ACCEL)

        p.prev_pos, p.pos = p.pos, p.pos + World.DT*vel

    func draw(p:Player):
        Color.PLAYER:draw_rectangle(p.pos, Player.SIZE)
