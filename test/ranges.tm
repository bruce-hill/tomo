
func main():
    >> Range(1, 5) == 1:to(5)
    = yes

    >> 1:to(5) == 5:to(1):reversed()
    = yes

    >> Range(1, 5) == Range(5, 10)
    = no

    >> [i for i in 3:to(5)]
    = [3, 4, 5]

    >> [i for i in 3:to(10):by(2)]
    = [3, 5, 7, 9]

    >> [i for i in 3:to(10):reversed():by(2)]
    = [10, 8, 6, 4]

    >> [i for i in (2:to(10):by(2)):by(2)]
    = [2, 6, 10]
