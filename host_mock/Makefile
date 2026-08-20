CXX      = g++
CC       = gcc
CXXFLAGS = -DHOST_MOCK -std=c++17 -g -Wall -Wno-unused-variable
CFLAGS   = -DHOST_MOCK -g -Wall

# host_mock first so mock esp_log.h shadows the real one
INCLUDES = -I. -I../main -I../components/ui/include \
           -I../components/ft8_lib -I../components/ft8_lib/vendor \
           -I../components/ft8_lib/vendor/ft8 \
           -I../components/adif/include \
           -I../components/station/include \
           -I../components/board_cardputer_adv/include \
           -I../components/qso_browse/include \
           -I../components/file_list/include

CORE_SRC   = ../main/autoseq.cpp
MOCK_SRC   = host_main.cpp host_mocks.cpp json_parser.cpp
# FT8 encode sources (uncomment to link real encoder):
# FT8_SRC  = ../components/ft8_lib/vendor/ft8/encode.c ../components/ft8_lib/vendor/ft8/message.c \
#            ../components/ft8_lib/vendor/ft8/crc.c ../components/ft8_lib/vendor/ft8/text.c

ALL_CXX_SRC = $(CORE_SRC) $(MOCK_SRC)
ALL_C_SRC   = $(FT8_SRC)

CXX_OBJ = $(ALL_CXX_SRC:.cpp=.o)
C_OBJ   = $(ALL_C_SRC:.c=.o)
OBJ     = $(CXX_OBJ) $(C_OBJ)

TARGET = host_test
UNIQUE_TARGET = host_test_unique_callsign
CANCEL_TARGET = host_test_beacon_cancel
ADIF_TARGET = host_test_adif_merge
STATION_TARGET = host_test_station
DECODE_SORT_TARGET = host_test_decode_sort
POWER_TARGET = host_test_power_hysteresis
QSO_BROWSE_TARGET = host_test_qso_browse
COPY_BLOCK_TARGET = host_test_copy_block
STATION_SAVE_TARGET = host_test_station_save_queue
FILE_LIST_TARGET = host_test_file_list
RADIO_PROFILE_TARGET = host_test_radio_profile

all: $(TARGET) $(UNIQUE_TARGET) $(CANCEL_TARGET) $(ADIF_TARGET) $(STATION_TARGET) \
     $(DECODE_SORT_TARGET) $(POWER_TARGET) $(QSO_BROWSE_TARGET) $(COPY_BLOCK_TARGET) \
     $(STATION_SAVE_TARGET) $(FILE_LIST_TARGET) $(RADIO_PROFILE_TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

UNIQUE_OBJ = ../main/autoseq.o host_mocks.o test_unique_callsign.o
$(UNIQUE_TARGET): $(UNIQUE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

CANCEL_OBJ = ../main/autoseq.o host_mocks.o test_beacon_cancel.o
$(CANCEL_TARGET): $(CANCEL_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

ADIF_OBJ = adif.o test_adif_merge.o
adif.o: ../components/adif/adif.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(ADIF_TARGET): $(ADIF_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

STATION_OBJ = station.o test_station.o
station.o: ../components/station/station.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(STATION_TARGET): $(STATION_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

DECODE_SORT_OBJ = test_decode_sort.o
$(DECODE_SORT_TARGET): $(DECODE_SORT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

RADIO_PROFILE_OBJ = test_radio_profile.o
$(RADIO_PROFILE_TARGET): $(RADIO_PROFILE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

POWER_OBJ = power_hysteresis.o test_power_hysteresis.o
power_hysteresis.o: ../components/board_cardputer_adv/power_hysteresis.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(POWER_TARGET): $(POWER_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

QSO_BROWSE_OBJ = qso_browse.o test_qso_browse.o
qso_browse.o: ../components/qso_browse/qso_browse.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(QSO_BROWSE_TARGET): $(QSO_BROWSE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

COPY_BLOCK_OBJ = test_copy_block.o
$(COPY_BLOCK_TARGET): $(COPY_BLOCK_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

STATION_SAVE_OBJ = station_save_queue.o test_station_save_queue.o
station_save_queue.o: ../components/station/station_save_queue.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(STATION_SAVE_TARGET): $(STATION_SAVE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

FILE_LIST_OBJ = qso_browse.o file_list.o file_list_queue.o test_file_list.o
file_list.o: ../components/file_list/file_list.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
file_list_queue.o: ../components/file_list/file_list_queue.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(FILE_LIST_TARGET): $(FILE_LIST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(UNIQUE_OBJ) $(CANCEL_OBJ) $(ADIF_OBJ) $(STATION_OBJ) \
	      $(DECODE_SORT_OBJ) $(POWER_OBJ) $(QSO_BROWSE_OBJ) $(COPY_BLOCK_OBJ) \
	      $(STATION_SAVE_OBJ) $(FILE_LIST_OBJ) $(RADIO_PROFILE_OBJ) \
	      $(TARGET) $(UNIQUE_TARGET) $(CANCEL_TARGET) $(ADIF_TARGET) $(STATION_TARGET) \
	      $(DECODE_SORT_TARGET) $(POWER_TARGET) $(QSO_BROWSE_TARGET) $(COPY_BLOCK_TARGET) \
	      $(STATION_SAVE_TARGET) $(FILE_LIST_TARGET) $(RADIO_PROFILE_TARGET)

.PHONY: all clean
