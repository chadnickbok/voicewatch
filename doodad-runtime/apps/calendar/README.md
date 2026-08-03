# Calendar + Agenda

Deterministic, full-screen calendar conformance package.

The app launches directly into a two-event day agenda. Tapping Design review
opens event detail, RSVP acceptance records a content-addressed confirmed
state, and Travel view exercises local time-zone and offline recovery
presentation.

Screens:

- Today agenda
- Design review detail
- RSVP confirmed
- Local travel time zone

Every transition crosses the domain-scoped mocked calendar capability before
mounting the next bounded AppSpec. The guest contains no renderer-specific
layout code.
