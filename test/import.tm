vectors := use ./_vectors.tm
use ./use_import.tm

func returns_vec(->vectors.Vec2)
	return vectors.Vec2(1, 2)

func returns_imported_type(->ImportedType)
	return get_value() # Imported from ./use_import.tm

func main()
	>> empty : [vectors.Vec2]
	assert returns_vec() == Vec2(x=1, y=2)

	>> imported : [ImportedType]
	assert returns_imported_type() == ImportedType("Hello")

	assert needs_initializing == 999999999999999999
