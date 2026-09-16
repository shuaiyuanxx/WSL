const std = @import("std");
pub fn main() !void {
    try std.io.getStdOut().writeAll("hello world from [zig]\n");
}
