# Defines a struct representing boxes on the terrain
use vectors

use ./world.tm
use ./color.tm

struct Box(pos:Vec2, size=Vec2(50, 50), color=Color.GRAY, blocking=yes):
    func draw(b:&Box):
        b.color:draw_rectangle(b.pos, b.size)
