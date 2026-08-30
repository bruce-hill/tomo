struct Person{name:Text, age:Int}
    max_age := 122

    func greet(self:Person)
        say("hi $(self.name)")

enum Shape(Point, Circle{radius:Float64}, Rectangle{width:Float64, height:Float64})
    func is_point(self:Shape -> Bool)
        match self
        case Point
            return yes
        else
            return no

func main()
    alice := Person{"Alice", 30}
    >> alice.age
    >> Shape.Circle{1.0}.is_point()
