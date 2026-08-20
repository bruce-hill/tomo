vectors := use ./_vectors.tm
use ./use_import.tm

func returns_vec(->vectors.Vec2)
	return vectors.Vec2{1, 2}

func returns_imported_type(->ImportedType)
	return get_value() # Imported from ./use_import.tm

test "using an imported module"
	>> empty : [vectors.Vec2]
	>> empty
	assert empty == []
	>> returns_vec()
	assert returns_vec() == vectors.Vec2{x=1, y=2}

test "importing a type"
	>> imported : [ImportedType]
	>> imported
	assert imported == []
	>> returns_imported_type()
	assert returns_imported_type() == ImportedType{"Hello"}

test "imported global initialization"
	>> needs_initializing
	assert needs_initializing == 999999999999999999
