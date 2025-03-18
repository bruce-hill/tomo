
func all_nums(nums:List(Int) -> Text):
	result := ""
	for num in nums:
		result ++= "$num,"
	else:
		return "EMPTY"
	return result

func labeled_nums(nums:List(Int) -> Text):
	result := ""
	for i,num in nums:
		result ++= "$i:$num,"
	else:
		return "EMPTY"
	return result

func table_str(t:Table(Text,Text) -> Text):
	str := ""
	for k,v in t:
		str ++= "$k:$v,"
	else: return "EMPTY"
	return str

func table_key_str(t:Table(Text,Text) -> Text):
	str := ""
	for k in t:
		str ++= "$k,"
	else: return "EMPTY"
	return str

func main():
	>> all_nums([10,20,30])
	= "10,20,30,"
	>> all_nums([:Int])
	= "EMPTY"

	>> labeled_nums([10,20,30])
	= "1:10,2:20,3:30,"
	>> labeled_nums([:Int])
	= "EMPTY"

	>> t := {"key1"="value1", "key2"="value2"}
	>> table_str(t)
	= "key1:value1,key2:value2,"
	>> table_str({:Text,Text})
	= "EMPTY"

	>> table_key_str(t)
	= "key1,key2,"
