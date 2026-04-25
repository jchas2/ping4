# This file belongs in the homebrew-ping4 tap repo at Formula/ping4.rb
# Setup:
#   1. Create a GitHub repo named homebrew-ping4
#   2. Copy this file to Formula/ping4.rb in that repo
#   3. Users can then install via:
#        brew tap <username>/ping4
#        brew install ping4
#
# The url and sha256 fields are updated automatically by the release workflow
# in the main ping4 repo via the TAP_GITHUB_TOKEN secret.

class Ping4 < Formula
  desc "IPv4 ping for macOS"
  homepage "https://github.com/#{tap.user}/ping4"
  url "https://github.com/#{tap.user}/ping4/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "PLACEHOLDER"
  version "1.0.0"
  license "MIT"
  head "https://github.com/#{tap.user}/ping4.git", branch: "main"

  depends_on "cmake" => :build
  depends_on :macos

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DPING4_VERSION=#{version}",
           *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/ping4"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/ping4 -V")
  end
end
