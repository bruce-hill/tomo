if yes
	>> [:Num32]
	= [] : [Num32]

if yes
	>> arr := [10, 20, 30]
	= [10, 20, 30]

	>> arr[1]
	= 10
	>> arr[-1]
	= 30

	>> #arr
	= 3

	sum := 0
	for x in arr
		sum += x
	>> sum
	= 60

	str := ""
	for i,x in arr
		str ++= "({i},{x})"
	>> str
	= "(1,10)(2,20)(3,30)"

if yes
	>> arr := [10, 20] ++ [30, 40]
	= [10, 20, 30, 40]

	>> arr ++= [50, 60]
	>> arr
	= [10, 20, 30, 40, 50, 60]

	>> arr ++= 70
	>> arr
	= [10, 20, 30, 40, 50, 60, 70]

if yes
	>> arr := [10, 20]
	>> copy := arr
	>> arr ++= 30
	>> arr
	= [10, 20, 30]
	>> copy
	= [10, 20]

// if yes
// 	>> arr := @[10, 20]
// 	>> copy := arr[]
// 	>> arr:insert(30)
// 	>> arr
// 	= @[10, 20, 30]
// 	>> copy
// 	= [10, 20]
