using System.Collections;
using System.Collections.Generic;
using UnityEngine;

// Список команд
/// nm 1 - двигаться и атаковать


public class gen_com {

	List <com_point> commands;

	gen_com ()
	{
		commands = new List<com_point> ();
	}
}

public struct com_point
{
	/// <summary>
	/// Номер приказа
	/// </summary>
	public int num_com;
	public float weight;
	public string name;
}

public struct des_point
{
	/// <summary>
	/// Номер решения
	/// </summary>
	public int num_des;
	public float weight;

	public des_point (int x , float y)
	{
		num_des = x;
		weight = y;
	}

	public void putweight (float x)
	{
		weight = x;
	}
}


