simulation.o: ticket.c
	gcc ticket.c -o simulation.o -lpthread -lrt
