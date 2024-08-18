func ping(x:Int)->[Text]:
	if x > 0:
		return ["ping: $x"] ++ pong(x-1)
	else:
		return ["ping: $x"]

func pong(x:Int)->[Text]:
	if x > 0:
		return ["pong: $x"] ++ ping(x-1)
	else:
		return ["pong: $x"]

func main():
	>> ping(3)
	= ["ping: 3", "pong: 2", "ping: 1", "pong: 0"]
