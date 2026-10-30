"""Actions and observations grouped by their gameplay domain."""
from .input import InputActions
from .actors import ActorsActions
from .session import SessionActions
from .story import StoryActions
from .join import JoinActions
from .world import WorldActions
from .dialogue import DialogueActions
from .travel import TravelActions
from .inventory import InventoryActions
from .mounts import MountsActions
from .combat import CombatActions
from .physics import PhysicsActions
from .magic import MagicActions
from .interface import InterfaceActions


class Actions(
    InputActions,
    ActorsActions,
    SessionActions,
    StoryActions,
    JoinActions,
    WorldActions,
    DialogueActions,
    TravelActions,
    InventoryActions,
    MountsActions,
    CombatActions,
    PhysicsActions,
    MagicActions,
    InterfaceActions,
):
    pass
