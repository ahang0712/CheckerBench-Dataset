# Patch-overlapping Python context after the fix

## `ihatemoney/models.py` — `class:Person` (lines 378-433)

```python
class Person(db.Model):
    class PersonQuery(BaseQuery):
        def get_by_name(self, name, project):
            return (
                Person.query.filter(Person.name == name)
                .filter(Person.project_id == project.id)
                .one()
            )

        def get(self, id, project=None):
            if not project:
                project = g.project
            return (
                Person.query.filter(Person.id == id)
                .filter(Person.project_id == project.id)
                .one()
            )

    query_class = PersonQuery

    # Direct SQLAlchemy-Continuum to track changes to this model
    __versioned__ = {}

    __table_args__ = {"sqlite_autoincrement": True}

    id = db.Column(db.Integer, primary_key=True)
    project_id = db.Column(db.String(64), db.ForeignKey("project.id"))
    bills = db.relationship("Bill", backref="payer")

    name = db.Column(db.UnicodeText)
    weight = db.Column(db.Float, default=1)
    activated = db.Column(db.Boolean, default=True)

    @property
    def _to_serialize(self):
        return {
            "id": self.id,
            "name": self.name,
            "weight": self.weight,
            "activated": self.activated,
        }

    def has_bills(self):
        """return if the user do have bills or not"""
        bills_as_ower_number = (
            db.session.query(billowers)
            .filter(billowers.columns.get("person_id") == self.id)
            .count()
        )
        return bills_as_ower_number != 0 or len(self.bills) != 0

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"<Person {self.name} for project {self.project.name}>"
```

## `ihatemoney/models.py` — `class:Person.PersonQuery` (lines 379-394)

```python
    class PersonQuery(BaseQuery):
        def get_by_name(self, name, project):
            return (
                Person.query.filter(Person.name == name)
                .filter(Person.project_id == project.id)
                .one()
            )

        def get(self, id, project=None):
            if not project:
                project = g.project
            return (
                Person.query.filter(Person.id == id)
                .filter(Person.project_id == project.id)
                .one()
            )
```

## `ihatemoney/models.py` — `function:Person.PersonQuery.get_by_name` (lines 380-385)

```python
        def get_by_name(self, name, project):
            return (
                Person.query.filter(Person.name == name)
                .filter(Person.project_id == project.id)
                .one()
            )
```

## `ihatemoney/models.py` — `function:Person.PersonQuery.get` (lines 387-394)

```python
        def get(self, id, project=None):
            if not project:
                project = g.project
            return (
                Person.query.filter(Person.id == id)
                .filter(Person.project_id == project.id)
                .one()
            )
```
