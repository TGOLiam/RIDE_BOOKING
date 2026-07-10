# Ride Booking System

Concurrent ride-booking simulation for CS0051 Parallel and Distributed Computing.

The project demonstrates how mutex synchronization prevents race conditions when multiple passenger requests try to reserve available drivers at the same time.

## Files

| File | Purpose |
|---|---|
| `main.cpp` | C++17 backend with dispatcher threads, booking/driver registries, mutex-protected state, monitor actor, and TCP command handler. |
| `SYSTEM_DESIGN.docx` | System design and team coordination document. |
| `README.md` | Project overview and run instructions. |

## Build and run

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o core2
./core2
```

The backend listens on:

```text
127.0.0.1:8000
```

## TCP command protocol

One newline-terminated spaced command per TCP connection.

| Command | Arguments | Response |
|---|---|---|
| `BOOK` | `px py dx dy` | `OK booking_id` |
| `BOOKING` | `booking_id` | `STATE driver_id retry_count` |
| `BOOKINGS` | — | `OK count booking_id state driver_id retry_count ...` |
| `DRIVER` | `driver_id` | `STATE booking_id pos_x pos_y` |
| `DRIVERS` | — | `OK count driver_id state booking_id pos_x pos_y ...` |
| `ACCEPT` | `driver_id booking_id` | `OK` or `ERR` |
| `REJECT` | `driver_id booking_id` | `OK` or `ERR` |
| `FINISH` | `driver_id booking_id` | `OK` or `ERR` |

Example:

```bash
printf 'BOOK 1 2 8 9\n' | nc 127.0.0.1 8000
printf 'DRIVERS\n' | nc 127.0.0.1 8000
```

## Concurrency design

The core race condition is double-booking: two dispatcher threads could otherwise observe the same driver as available and assign that driver to different bookings.

The backend prevents this with:

- per-driver mutexes for driver state and assignment
- per-booking mutexes for booking state
- registry mutexes for container access
- dispatcher threads that reserve drivers atomically
- monitor actor that expires unaccepted reservations

Primary lock order:

```text
Driver::mtx -> BookingRegistry::mtx -> Booking::mtx
```
