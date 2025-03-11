
use ./player.tm
use ./raylib.tm
use ./box.tm

# Return a displacement relative to `a` that will push it out of `b`
func solve_overlap(a_pos:Vector2, a_size:Vector2, b_pos:Vector2, b_size:Vector2 -> Vector2):
    a_left := a_pos.x
    a_right := a_pos.x + a_size.x
    a_top := a_pos.y
    a_bottom := a_pos.y + a_size.y

    b_left := b_pos.x
    b_right := b_pos.x + b_size.x
    b_top := b_pos.y
    b_bottom := b_pos.y + b_size.y

    # Calculate the overlap in each dimension
    overlap_x := (a_right _min_ b_right) - (a_left _max_ b_left)
    overlap_y := (a_bottom _min_ b_bottom) - (a_top _max_ b_top)

    # If either axis is not overlapping, then there is no collision:
    if overlap_x <= 0 or overlap_y <= 0:
        return Vector2(0, 0)

    if overlap_x < overlap_y:
        if a_right > b_left and a_right < b_right:
            return Vector2(-(overlap_x), 0)
        else if a_left < b_right and a_left > b_left:
            return Vector2(overlap_x, 0)
    else:
        if a_top < b_bottom and a_top > b_top:
            return Vector2(0, overlap_y)
        else if a_bottom > b_top and a_bottom < b_bottom:
            return Vector2(0, -overlap_y)

    return Vector2(0, 0)

struct World(player:@Player, goal:@Box, boxes:@[@Box], dt_accum=Num32(0.0), won=no):
    DT := (Num32(1.)/Num32(60.))!
    STIFFNESS := Num32(0.3)

    func update(w:@World, dt:Num32):
        w.dt_accum += dt
        while w.dt_accum > 0:
            w:update_once()
            w.dt_accum -= World.DT

    func update_once(w:@World):
        w.player:update()

        if solve_overlap(w.player.pos, Player.SIZE, w.goal.pos, w.goal.size) != Vector2(0,0):
            w.won = yes

        # Resolve player overlapping with any boxes:
        for i in 3:
            for b in w.boxes:
                w.player.pos += World.STIFFNESS * solve_overlap(w.player.pos, Player.SIZE, b.pos, b.size)

    func draw(w:@World):
        for b in w.boxes:
            b:draw()
        w.goal:draw()
        w.player:draw()

        if w.won:
            DrawText(CString("WINNER"), GetScreenWidth()/Int32(2)-Int32(48*3), GetScreenHeight()/Int32(2)-Int32(24), 48, Color(0,0,0))

    func load_map(w:@World, map:Text):
        if map:has($/[]/):
            map = map:replace_all({$/[]/="#", $/@{1..}/="@", $/  /=" "})
        w.boxes = @[:@Box]
        box_size := Vector2(50., 50.)
        for y,line in map:lines():
            for x,cell in line:split():
                if cell == "#":
                    pos := Vector2((Num32(x)-1) * box_size.x, (Num32(y)-1) * box_size.y)
                    box := @Box(pos, size=box_size, color=Color(0x80,0x80,0x80))
                    w.boxes:insert(box)
                else if cell == "@":
                    pos := Vector2((Num32(x)-1) * box_size.x, (Num32(y)-1) * box_size.y)
                    pos += box_size/Num32(2) - Player.SIZE/Num32(2)
                    w.player = @Player(pos,pos)
                else if cell == "?":
                    pos := Vector2((Num32(x)-1) * box_size.x, (Num32(y)-1) * box_size.y)
                    w.goal.pos = pos

