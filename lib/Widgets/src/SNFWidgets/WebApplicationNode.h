#pragma once

// WebApplicationNode has been renamed to ApplicationNode.
// This header is kept only as a forwarding shim for code that has not yet
// been updated.  New code should include <SNFWidgets/ApplicationNode.h>
// and use snf::widgets::ApplicationNode directly.
#include "ApplicationNode.h"

namespace snf {
namespace widgets {
using WebApplicationNode = ApplicationNode;
}  // namespace widgets
}  // namespace snf

