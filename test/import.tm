vectors := use ../examples/vectors.tm
use ./use_import.tm

func returns_vec()->vectors.Vec2:
	return vectors.Vec2(1, 2)

func returns_imported_type()->ImportedType:
	return get_value() # Imported from ./use_import.tm

func main():
	>> [:vectors.Vec2]
	>> returns_vec()
	= Vec2(x=1, y=2)

	>> [:ImportedType]
	>> returns_imported_type()
	= ImportedType("Hello")

	>> needs_initializing # imported from ./use_import.tm
	= 999999999999999999
