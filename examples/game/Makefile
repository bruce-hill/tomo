

game: game.tm box.tm color.tm player.tm world.tm
	tomo -e game.tm

# Disable built-in makefile rules:
%: %.c
%.o: %.c
%: %.o

clean:
	rm -vf game *.tm.*

play: game
	./game

.PHONY: play, clean
