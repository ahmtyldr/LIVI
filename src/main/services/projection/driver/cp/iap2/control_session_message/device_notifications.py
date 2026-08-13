from dataclasses import dataclass
from typing import Annotated, Optional

from iap2.control_session_message import csm, Int8, Int16, Int64


@csm(0x4E0B)
@dataclass
class DeviceTimeUpdate:
    seconds_since_reference_date: Annotated[Optional[Int64], 0] = None
    time_zone_offset_minutes: Annotated[Optional[Int16], 1] = None
    daylight_savings_offset_minutes: Annotated[Optional[Int8], 2] = None
