@0x9a30282a17391632;  # unique file ID

struct TlmPayload {
  id @0 :UInt64;
  command @1 :UInt8;
  address @2 :UInt64;
  dataLength @3 :UInt32;
  byteEnableLength @4 :UInt32;
  axuserLength @5 :UInt32;
  xuserLength @6 :UInt32;
  streamingWidth @7 :UInt32;
  response @8 :Int8;
  payload @9 :Data; # This will hold the combined data, byte_enable, axuser, and xuser data
}