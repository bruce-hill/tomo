imported := use ./use_import

func asdf()->imported.ImportedType
	return imported.get_value()

func main()
	>> [:imported.ImportedType]
	>> asdf()
	= ImportedType(name="Hello")
