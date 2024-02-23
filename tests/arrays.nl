>> [:Num32]
= [] : [Num32]

>> arr := [10, 20, 30]
= [10, 20, 30]

>> arr[1]
= 10
>> arr[-1]
= 30

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

>> arr2 := [10, 20] ++ [30, 40]
= [10, 20, 30, 40]

>> arr2 ++= [50, 60]
>> arr2
= [10, 20, 30, 40, 50, 60]

>> arr2 ++= 70
>> arr2
= [10, 20, 30, 40, 50, 60, 70]
