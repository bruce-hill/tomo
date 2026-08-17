test "unicode identifiers"
	>> café := 42
	assert café == 42
	>> λ := 10
	>> naïve := λ * 2
	assert naïve == 20

test "NFC normalization of identifiers"
	# Declared with precomposed é (U+00E9):
	>> résumé := 7
	# Referenced with decomposed e + combining acute (U+0301):
	assert résumé == 7
