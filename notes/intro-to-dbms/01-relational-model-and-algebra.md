# Relational Model & Algebra

## Database

Organized collection of inter-related data that models some aspect of the real-world

> A database management system (DBMS) supports definitions, creation, querying, update and adminstration of databases in accordance with some data model

A **data model** is a collection of concepts for describing the data in a database.
> Rules that define the types of things that could exist an how they relate

A **schema** is a description of a particular collection of data, using a given data model
> - This defines the structure of database for a data model 
> - Otherwise, you have random bits no meaning

A **relation** is an unordered set that contain the relationship of attributes that represnt entities

A **tuple** is a set of attributes values (aka it's **domain**) in the relation 
> - Values are atomic / scalar normally
> - The special value NULL is a memmber of every domain (if allowed)

A relations **parimay key** uniquely identifies a single tuple / record.

Som DBMS auto create an intenal primary key if a table does not define one 

DBMS can auto-gen unique primary keys via an **indentiy column**
> - IDENTITY (SQL Standard)
> - SEQUENCE (PostgreSQL / Oracle)
> - AUTO_INCREMENT (MySQL)

A **foreign** key specifies that an attribute from one relation maps to a tuple in another relation

**Constraints:** a user conditions that must hold for any ionstance of the database
> - Can validate data within  a single tuple or across entire relation(s)
> - DBMS prevents modifications that violate any constraint

Unique ey and referential (fkey) constrainst are the most common 

SQL:92 supports global asserts but these are rarely used (too slow)

## Data Manipulation Languages (DML)
The API that a DBMS exposes to applications to store and retrive information form a database

**Procedural:** The query specifies the (high-level) strategy to find the desired result based on sets / bags
> Relation Algebra

**Non-Procedural (Declarative):** The query specifies only what data is wanted and not how to find it
> Relation Calculus

# Relational Algebra
- Fundamental operations to retrive and manipulate tuples in a relation

- Each operator takes one or more relations as its inputs and outputs a new relation
> - We can "chain" operators together to create more complex operations

# Relational Algebra Operators

| Symbol | Operator Name |
|--------|---------------|
| σ      | Select        |
| π      | Projection    |
| ∪      | Union         |
| ∩      | Intersection  |
| −      | Difference    |
| ×      | Product       |
| ⋈      | Join          |

